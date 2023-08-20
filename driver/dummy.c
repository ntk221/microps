// ダミーデバイスの仕様
// 入力 … なし（データを受信することはない）
// 出力 … データを破棄

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include "util.h"
#include "net.h"

#include "platform.h"

#define DUMMY_MTU UINT16_MAX /* maximum size of IP datagram */
#define DUMMY_IRQ INTR_IRQ_BASE 

// DUMMY_IRQに対応した割り込みハンドラ
// テスト用なので、呼び出されたことを確認するために、debugfを読んでるだけ
static int dummy_isr(unsigned int irq, void *id)
{
    debugf("irq=%u, dev=%s", irq, ((struct net_device *)id)->name);
    return 0;
}

static int dummy_transmit(struct net_device *dev, uint16_t type, const uint8_t *data, size_t len, const void *dst)
{
    debugf("dev=%s, type=0x%04x, len=%zu", dev->name, type, len);
    debugdump(data, len);
    /* drop data */
    intr_raise_irq(DUMMY_IRQ); // テスト用に割り込みを発生させる
    return 0;
}

// デバイスドライバが実装している関数ポインタを登録する
static struct net_device_ops dummy_ops = {
    .transmit = dummy_transmit, // 送信関数のみ登録しておく
};

struct net_device *dummy_init(void)
{
    struct net_device *dev;

    // デバイスの生成処理
    dev = net_device_alloc();
    if (!dev) {
        errorf("net_device_alloc() failure");
        return NULL;
    }
    // デバイスの初期化処理
    dev->type = NET_DEVICE_TYPE_DUMMY; // デバイスの種類はnet.hで定義されている
    dev->mtu = DUMMY_MTU;
    dev->hlen = 0; /* non header */
    dev->alen = 0; /* non address */
    dev->ops = &dummy_ops; // デバイスドライバが実装している関数ポインタを登録する
    if (net_device_register(dev) == -1) {
        errorf("net_device_register() failure");
        return NULL;
    }
    intr_request_irq(DUMMY_IRQ, dummy_isr, INTR_IRQ_SHARED, dev->name, dev); // 割り込みハンドラとしてdummy_isrを登録しとこ
    debugf("initialized, dev=%s", dev->name);
    return dev;
}

