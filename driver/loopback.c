#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#include "util.h"
#include "net.h"

#define LOOPBACK_MTU UINT16_MAX /*maximum size of IP datagram */
#define LOOPBACK_QUEUE_LIMIT 16
#define LOOPBACK_IRQ (INTR_IRQ_BASE+1)

#define PRIV(x) ((struct loopback*)x->priv)

// ループバックデバイスのドライバ内で使用するプライベートなデータを格納するための構造体
struct loopback {
    int irq;
    mutex_t mutex;
    struct queue_head queue;
};

/**
    キューのエントリの構造体
    ・データ本体と付随する情報（メタデータ）を格納
    ・この構造体の最後のメンバは “フレキブル配列メンバ” と呼ばれる特殊なメンバ変数
　      ・構造体の最後にだけ配置できるサイズ不明の配列
　      ・メンバ変数としてアクセスできるが構造体のサイズには含まれない（必ずデータ部分も含めてメモリを確保すること）

    キュー自体は、util.hで定義されている
*/
struct loopback_queue_entry {
    uint16_t type;
    size_t len;
    uint8_t data[]; /* flexible array member */
};

static int loopback_transmit(struct net_device *dev, uint16_t type, \
               const uint8_t *data, size_t len, const void *dst)
{
    struct loopback_queue_entry *entry;
    unsigned int num;

    // キューへのアクセスをmutexで保護する（unlockを忘れずに）
    mutex_lock(&PRIV(dev)->mutex);
    if (PRIV(dev)->queue.num >= LOOPBACK_QUEUE_LIMIT) { // キューが保持できる容量の上限を超えていたらエラーを返す
        mutex_unlock(&PRIV(dev)->mutex);
        errorf("queue is full");
        return -1;
    }

    // キューに格納するentryのポインタを用意する
    entry = memory_alloc(sizeof(*entry) + len);
    if (!entry) {
        mutex_unlock(&PRIV(dev)->mutex);
        errorf("memory_alloc() failure");
        return -1;
    }
    // メタデータの設定とデータ本体のコピー
    entry->type = type;
    entry->len = len;
    memcpy(entry->data, data, len);

    // エントリをキューに追加する
    queue_push(&PRIV(dev)->queue, entry);
    num = PRIV(dev)->queue.num;
    mutex_unlock(&PRIV(dev)->mutex); // キューに追加したらmutexを解放

    debugf("queue pushed (num:%u), dev=%s, type=0x%04x, len=%zd", num, dev->name, type, len);
    debugdump(data, len);

    // 割り込みを発生させる
    intr_raise_irq(PRIV(dev)->irq);
    return 0;
}

static int loopback_isr(unsigned int irq, void *id)
{
    struct net_device *dev;
    struct loopback_queue_entry *entry;

    dev = (struct net_device *)id;
    // キューへのアクセスをmutexで保護
    mutex_lock(&PRIV(dev)->mutex);
    while (1) {
        entry = queue_pop(&PRIV(dev)->queue); // キューからエントリを取り出す
        if (!entry) { // 取り出すエントリがなくなったらループを抜ける
            break;
        }
        debugf("queue popped (num:%u), dev=%s, type=0x%04x, len=%zd", PRIV(dev)->queue.num, dev->name, entry->type, entry->len);
        debugdump(entry->data, entry->len);
        
        // net_input_handler() に受信データ本体と付随する情報を渡す
        net_input_handler(entry->type, entry->data, entry->len, dev);
        memory_free(entry);
    }
    mutex_unlock(&PRIV(dev)->mutex);
    return 0;
}

static struct net_device_ops loopback_ops = {
    .transmit = loopback_transmit,
};

/*
    
    intr_request_irq(DUMMY_IRQ, dummy_isr, INTR_IRQ_SHARED, dev->name, dev); // 割り込みハンドラとしてdummy_isrを登録しとこ
    debugf("initialized, dev=%s", dev->name);
    return dev;
*/

struct net_device *loopback_init(void)
{
    struct net_device *dev;
    struct loopback *lo;

    // deviceを準備する
    dev = net_device_alloc();
    if (!dev) {
        errorf("net_device_alloc() failure");
        return NULL;
    }
    dev->type = NET_DEVICE_TYPE_LOOPBACK; // デバイスの種類はnet.hで定義されている
    dev->mtu = LOOPBACK_MTU;
    dev->hlen = 0; // ?
    dev->alen = 0; // ?
    dev->ops = &loopback_ops; // デバイスドライバが実装している関数ポインタを登録する

    // loopbackデバイス用のプライベートなデータを準備する
    lo = memory_alloc(sizeof(*lo));
    if (!lo) {
        errorf("memory_alloc() failure");
        return NULL;
    }
    lo->irq = LOOPBACK_IRQ;
    mutex_init(&lo->mutex);
    queue_init(&lo->queue);

    dev->priv = lo; // プライベートなデータをデバイス構造体に格納する（ドライバの関数が呼び出される際にはデバイス構造体が渡されるのでここから取り出す）

    // デバイスの登録と割り込みハンドラの設定
    if (net_device_register(dev) == -1) {
        errorf("net_device_register() failure");
        return NULL;
    }
    intr_request_irq(LOOPBACK_IRQ, loopback_isr, INTR_IRQ_SHARED, dev->name, dev); // 割り込みハンドラとしてdummy_isrを登録しとこ
    debugf("initialized, dev=%s", dev->name);


    debugf("initialized, dev=%s", dev->name);
    return dev;
}