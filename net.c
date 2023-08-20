#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "platform.h"

#include "util.h"
#include "net.h"

/* NOTE: if you want to add/delete the entries after net_run(), you need to protect these lists with a mutex. */
static struct net_device *devices;

struct net_device *
net_device_alloc(void)
{
    struct net_device *dev;

    // デバイス構造体のサイズのメモリを確保
    dev = memory_alloc(sizeof(*dev));
    if (!dev) {
        errorf("memory_alloc() failure");
        return NULL;
    }
    return dev;
}

/* NOTE: must not be call after net_run() */
// デバイスリストにデバイスを登録
int net_device_register(struct net_device *dev)
{
    static unsigned int index = 0;

    dev->index = index++; // デバイスのインデックスを設定
    snprintf(dev->name, sizeof(dev->name), "net%d", dev->index);
    dev->next = devices; // デバイスリストの先頭に追加
    devices = dev;       // デバイスリストの先頭を更新
    infof("registered, dev=%s, type=0x%04x", dev->name, dev->type);
    return 0;
}

// deviceを受け取って、openされていない場合、openする
// すでにopenされている場合はエラーを返す
static int net_device_open(struct net_device *dev)
{
    if (NET_DEVICE_IS_UP(dev)) { // デバイスが既に開かれているかどうか
        errorf("already opened, dev=%s", dev->name);
        return -1;
    }
    // open関数が登録されていない場合はスキップ
    if (dev->ops->open) {
        if (dev->ops->open(dev) == -1) {  // open関数の呼び出し、失敗した場合はエラーを返す
            errorf("failure, dev=%s", dev->name);
            return -1;
        }
    }
    dev->flags |= NET_DEVICE_FLAG_UP; // UPフラグを立てる
    infof("dev=%s, state=%s", dev->name, NET_DEVICE_STATE(dev));
    return 0;
}

// net_device_openのclose版
static int net_device_close(struct net_device *dev)
{
    if (!NET_DEVICE_IS_UP(dev)) {
        errorf("not opened, dev=%s", dev->name);
        return -1;
    }
    if (dev->ops->close) {
        if (dev->ops->close(dev) == -1) {
            errorf("failure, dev=%s", dev->name);
            return -1;
        }
    }
    dev->flags &= ~NET_DEVICE_FLAG_UP;
    infof("dev=%s, state=%s", dev->name, NET_DEVICE_STATE(dev));
    return 0;
}

// UP状態のdeviceについて, 送信関数(transmit)を呼び出す
// UP状態でないときはエラーを返す
int net_device_output(struct net_device *dev, uint16_t type, 
                    const uint8_t *data, size_t len, const void *dst)
{
    if (!NET_DEVICE_IS_UP(dev)) {
        errorf("not opened, dev=%s", dev->name);
        return -1;
    }
    if (len > dev->mtu) {
        errorf("too long, dev=%s, mtu=%u, len=%zu", dev->name, dev->mtu, len);
        return -1;
    }
    debugf("dev=%s, type=0x%04x, len=%zu", dev->name, type, len);
    debugdump(data, len);
    if (dev->ops->transmit(dev, type, data, len, dst) == -1) {
        errorf("device transmit failure, dev=%s, len=%zu", dev->name, len);
        return -1;
    }
    return 0;
}

// デバイスが受信したパケットをプロトコルスタックに渡す関数
int net_input_handler(uint16_t type, const uint8_t *data,
                        size_t len, struct net_device *dev)
{
    // TODO: ここで受信したパケットをプロトコルスタックに渡す
    debugf("dev=%s, type=0x%04x, len=%zu", dev->name, type, len);
    debugdump(data, len);
    return 0;
}

int net_run(void)
{
    struct net_device *dev;

    // 割り込み処理機構を起動
    if (intr_run() == -1) {
        errorf("intr_run() failure");
        return -1;
    }

    debugf("open all devices...");
    for (dev = devices; dev; dev = dev->next) { // 登録されているすべてのデバイスをopenする
        net_device_open(dev);
    }
    debugf("running...");
    return 0;

}

void
net_shutdown(void)
{
    struct net_device *dev;

    intr_shutdown(); // 割り込み処理機構の終了
    debugf("close all devices...");
    for (dev = devices; dev; dev = dev->next) { // 登録されているすべてのデバイスをcloseする
        net_device_close(dev);
    }
    debugf("shutting down");
}

int
net_init(void)
{
    // 割り込み処理機構の初期化
    if (intr_init() == -1) {
        errorf("intr_init() failure");
        return -1;
    }
    // 今は何もしない
    infof("initialized");
    return 0;
}