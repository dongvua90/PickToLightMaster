#ifndef P2L_UART_H
#define P2L_UART_H

#include "p2l_common.h"

void P2L_UART_Init(void);
void P2L_UART_Task(void);

/* Bat dau (hoac lap lai) qua trinh quet/gan ID cho toan bo chuoi slave. */
void P2L_UART_StartIdScan(void);

/* Xep lenh RESET_QTY (broadcast) xuong UART1 cho chuoi slave. */
void P2L_UART_SendResetQty(void);

/* Xep lenh SAVE_ID (broadcast) xuong UART1, yeu cau slave luu ID hien tai vao bo nho khong bay hoi. */
void P2L_UART_SendSaveId(void);

/* Duoc goi tu CDC_Receive_FS() khi USB CDC nhan duoc du lieu tu PC. */
void P2L_USB_OnReceive(const uint8_t *buf, uint32_t len);

/* Duoc goi tu CDC_TransmitCplt_FS() khi USB CDC gui xong du lieu ve PC. */
void P2L_USB_OnTransmitCplt(void);

#endif
