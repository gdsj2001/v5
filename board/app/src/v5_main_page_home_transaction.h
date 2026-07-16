#ifndef V5_MAIN_PAGE_HOME_TRANSACTION_H
#define V5_MAIN_PAGE_HOME_TRANSACTION_H

#include "v5_main_page.h"
#include "v5_command_gate_ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

int v5_main_page_home_transaction_start(
    V5MainPage *page,
    V5MainPageActionReport *report,
    unsigned int timeout_ms);
int v5_main_page_home_transaction_poll(V5MainPage *page);
void v5_main_page_home_transaction_reset_after_estop(V5MainPage *page);
int v5_main_page_home_transaction_active(void);
int v5_main_page_home_transaction_status(V5CommandGateHomeStatus *status);
int v5_main_page_home_transaction_format_status_cn(
    const V5CommandGateHomeStatus *status,
    char *text,
    size_t text_cap);

#ifdef V5_MAIN_PAGE_HOME_TRANSACTION_TEST_HOOKS
typedef int (*V5MainPageHomeTransactionSendHook)(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5CommandGateResult *result,
    unsigned int timeout_ms);
typedef int (*V5MainPageHomeTransactionProgressHook)(
    unsigned long long run_id,
    unsigned int generation,
    V5CommandGateHomeStatus *status,
    unsigned int timeout_ms);
void v5_main_page_home_transaction_set_send_hook(V5MainPageHomeTransactionSendHook hook);
void v5_main_page_home_transaction_set_progress_hook(V5MainPageHomeTransactionProgressHook hook);
#endif

#ifdef __cplusplus
}
#endif

#endif
