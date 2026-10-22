#ifndef P2L_COMMON_H
#define P2L_COMMON_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Giao thuc khung UART giua master va chuoi slave, giu nguyen dinh dang
 * voi PickToLightSlave (xem readme.txt cua project slave). Khong CRC,
 * khong DST/SRC. */
#define P2L_UART_FRAME_SOF            0xA5u
#define P2L_UART_CMD_SET_ID           0x02u   /* ghi ID theo chuoi cho cac slave */
#define P2L_UART_CMD_SET_QTY          0x03u   /* ghi gia tri hien thi cho cac slave */
#define P2L_UART_CMD_ID_RESULT        0x04u   /* phan hoi ID lon nhat cua slave cuoi */
#define P2L_UART_CMD_SET_ID_ACK       0x05u   /* xac nhan da nhan SET_ID */
#define P2L_UART_CMD_RESET_QTY        0x06u   /* broadcast dat QTY=0 cho moi slave */

/* Lenh rieng cua master, chi trao doi qua USB CDC voi PC, khong xuat hien tren day UART. */
#define P2L_UART_CMD_SAVE_ID          0x07u   /* khop voi slave: lenh USB CDC rieng, giu cho khong trung ma */
#define P2L_UART_CMD_SCAN_SLAVES      0x08u   /* PC yeu cau quet lai chuoi slave: [A5][08] */
#define P2L_UART_CMD_GET_SLAVE_COUNT  0x09u   /* PC hoi so slave da quet duoc: [A5][09] -> [A5][09][COUNT] */

#define P2L_UART_ID_ACK_TIMEOUT_MS    1800u    /* thoi gian cho ACK tu slave truoc khi ket luan het chuoi */

#define P2L_UART_MAX_SLAVES           24u   /* gioi han ID hop le cua mot slave */
#define P2L_UART_MAX_SLAVES_PER_FRAME 24u   /* so cap [ID][VALUE] toi da trong 1 khung SET_QTY */
#define P2L_UART_MAX_PAYLOAD_LEN      (P2L_UART_MAX_SLAVES_PER_FRAME * 2u)
#define P2L_UART_MAX_FRAME_LEN        (3u + P2L_UART_MAX_PAYLOAD_LEN)

typedef struct
{
  uint8_t scanned_slave_count;  /* so slave quet duoc lan gan nhat, 0 = chua quet hoac khong co slave */
  uint8_t scan_in_progress;
} p2l_master_state_t;

extern UART_HandleTypeDef huart1;
extern p2l_master_state_t g_p2l;

#endif
