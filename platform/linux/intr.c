#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "platform.h"

#include "util.h"

struct irq_entry {
    struct irq_entry *next; // 次のirq_entryへのポインタ
    unsigned int irq;       // 割り込み番号(IRQ番号)
    int (*handler)(unsigned int irq, void *dev); // 割り込みが発生したときに呼び出される割り込みハンドラ
    int flags; // フラグ（INTR_IRQ_SHARED が指定された場合はIRQ番号を共有可能）
    char name[16];
    void *dev; // 割り込みの発生源となるデバイス
};

/* NOTE: if you want to add/delete the entries after intr_run(), you need to protect these lists with a mutex. */
// irq_entryのリストの先頭へのポインタ
static struct irq_entry *irqs;

static sigset_t sigmask; // シグナルマスク用

static pthread_barrier_t barrier; // スレッド間の同期用

/**
 * @brief IRQ番号が既に登録されている場合、IRQ番号の共有が許可されているかどうかチェック。どちらかが共有を許可してない場合はエラーを返す。
 * 
 */
int intr_request_irq(unsigned int irq, int (*handler)(unsigned int irq, void *dev), int flags, const char *name, void *dev)
{
    struct irq_entry *entry;

    debugf("irq=%u, flags=%d, name=%s", irq, flags, name);
    for (entry = irqs; entry; entry = entry->next) {
        if (entry->irq == irq) {
            if (entry->flags ^ INTR_IRQ_SHARED || flags ^ INTR_IRQ_SHARED) {
                errorf("conflicts with already registered IRQs");
                return -1;
            }
        }
    }

	// IRQリストに新しいエントリを追加する
	// メモリ確保
	entry = memory_alloc(sizeof(*entry));
	if (!entry) {
	    errorf("memory_alloc() failure");
	    return -1;
	}
	// IRQ構造体の値を設定
	entry->irq = irq;
	entry->handler = handler;
	entry->flags = flags;
	strncpy(entry->name, name, sizeof(entry->name)-1);
	// リストの先頭に追加
	entry->dev = dev;
	entry->next = irqs;
	irqs = entry;
	// シグナルマスクに新しいシグナルを追加
	sigaddset(&sigmask, irq);
	debugf("registered: irq=%u, name=%s", irq, name);

    return 0;
}

static pthread_t tid; // 割り込み処理スレッドのtid

int
intr_raise_irq(unsigned int irq)
{
	return pthread_kill(tid, (int)irq); // 割り込み処理スレッドにシグナルを送信
}


// 割り込みを補足して振り分ける
static void * intr_thread(void *arg)
{
    int terminate = 0, sig, err;
    struct irq_entry *entry;

    debugf("start...");
    pthread_barrier_wait(&barrier); // メインスレッドと同期をとるための処理
    while (!terminate) {
        err = sigwait(&sigmask, &sig); // 割り込みに見立てたシグナルが発生するまで待機;
        if (err) {
            errorf("sigwait() %s", strerror(err));
            break;
        }
		// 発生したシグナルによって処理を分岐する
        switch (sig) {
        case SIGHUP: // SIGHUP: 割り込みスレッドへ終了を通知するためのシグナル を受け取ったときは、terminate を 1 にしてbreak
            terminate = 1;
            break;
        default: // デバイス割り込み用のシグナルを受け取ったときは、IRQリストを巡回して、割り込みハンドラを呼び出す
            for (entry = irqs; entry; entry = entry->next) { // IRQリストを巡回する
                if (entry->irq == (unsigned int)sig) { // IRQ番号が一致するエントリを見つけたら、その割り込みハンドラを呼び出す
                    debugf("irq=%d, name=%s", entry->irq, entry->name);
                    entry->handler(entry->irq, entry->dev);
                }
            }
            break;
        }
    }
    debugf("terminated");
    return NULL;
}

// 割り込み処理機構の開始
int intr_run(void)
{
    int err;

	// シグナルマスクの設定
    err = pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
    if (err) {
        errorf("pthread_sigmask() %s", strerror(err));
        return -1;
    }
	// 割り込み処理用のスレッドを起動する
    err = pthread_create(&tid, NULL, intr_thread, NULL);
    if (err) {
        errorf("pthread_create() %s", strerror(err));
        return -1;
    }
	// スレッドが動き出すまで待つ（他のスレッドが同じように pthread_barrier_wait() を呼び出し、バリアのカウントが指定の数になるまでスレッドを停止する）
    pthread_barrier_wait(&barrier);
    return 0;
}

// 割り込み処理機構を終了する
void intr_shutdown(void)
{
    if (pthread_equal(tid, pthread_self()) != 0) {
        /* Thread not created. */
        return;
    }
    pthread_kill(tid, SIGHUP); // 割り込みスレッドに終了を通知するためのシグナル(SIGHUP)を送信
    pthread_join(tid, NULL); // 割り込み処理スレッドの終了を待つ
}

// 割り込み処理機構の初期化
int intr_init(void)
{
    tid = pthread_self(); // スレッドidの初期値をmainスレッドのtidで設定
    pthread_barrier_init(&barrier, NULL, 2); // カウントを2で初期化
    sigemptyset(&sigmask); // シグナル集合を初期化
    sigaddset(&sigmask, SIGHUP); //シグナル集合にSIGHUPを追加(割り込みスレッド終了通知用)
    return 0;
}