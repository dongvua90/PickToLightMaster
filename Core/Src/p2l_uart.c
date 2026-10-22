#include "p2l_uart.h"
#include "usbd_cdc_if.h"

#define P2L_UART1_RXBUF_SIZE 128u
#define P2L_USB_RXBUF_SIZE   128u
#define P2L_BTN_DEBOUNCE_MS  30u
#define P2L_SCAN_STARTUP_DELAY_MS 1000u
#define P2L_SCAN_SAME_RESULT_LIMIT 5u

p2l_master_state_t g_p2l;

/* Buffer HAL ghi du lieu nhan ve tren UART1 (dung cho HAL_UARTEx_ReceiveToIdle_IT). */
static uint8_t uart1_rx_buf[P2L_UART1_RXBUF_SIZE];
/* Ban sao khung dang cho P2L_UART_Task() xu ly (tach khoi ngat). */
static uint8_t uart1_rx_frame[P2L_UART1_RXBUF_SIZE];
static volatile uint16_t uart1_rx_frame_len = 0u;

/* Khung nhan tu USB CDC (lenh cua PC), duoc P2L_USB_OnReceive() sao chep sang day. */
static uint8_t usb_rx_frame[P2L_USB_RXBUF_SIZE];
static volatile uint16_t usb_rx_frame_len = 0u;

/* Hang doi gui xuong UART1 (chuoi slave). */
static uint8_t uart1_tx_buf[P2L_UART_MAX_FRAME_LEN];
static uint16_t uart1_pending_len = 0u;
static volatile uint8_t uart1_tx_busy = 0u;

/* Hang doi gui len USB CDC (ve PC). */
static uint8_t usb_tx_buf[8];
static uint16_t usb_pending_len = 0u;
static volatile uint8_t usb_tx_busy = 0u;

/* Cho ACK tu slave dau tien sau khi vua gui SET_ID trong luc quet. */
static uint32_t id_ack_deadline = 0u;
static uint8_t id_ack_wait_active = 0u;
static uint8_t id_scan_attempt_count = 0u;
static uint8_t id_scan_same_result_count = 0u;
static uint8_t id_scan_last_result = 0u;
static uint8_t id_scan_last_result_valid = 0u;

static void usb_send_id_result(uint8_t count);

/* Tu dong quet ID luc khoi dong - dang tam vo hieu hoa, thay bang gui khung dummy ben duoi.
static uint32_t startup_scan_tick = 0u;
static uint8_t startup_scan_pending = 0u;
*/

/* Gui khung dummy [A5][09] 24 lan, cach nhau 50ms, de kiem tra duong truyen UART1 luc khoi dong. */
#define P2L_DUMMY_CMD             0x09u
#define P2L_DUMMY_SEND_COUNT      24u
#define P2L_DUMMY_SEND_PERIOD_MS  50u

static uint8_t dummy_send_remaining = 0u;
static uint32_t dummy_send_next_tick = 0u;

/* Debounce nut BTN_ENTER, kich hoat RESET_QTY khi nhan. */
static GPIO_PinState btn_stable_state = GPIO_PIN_SET;
static GPIO_PinState btn_last_reading = GPIO_PIN_SET;
static uint32_t btn_last_reading_tick = 0u;

static void uart1_queue_send(const uint8_t *data, uint16_t len)
{
  if (len > sizeof(uart1_tx_buf))
  {
    return;
  }

  memcpy(uart1_tx_buf, data, len);
  uart1_pending_len = len;
}

static void uart1_try_send_pending(void)
{
  if ((uart1_pending_len > 0u) && !uart1_tx_busy)
  {
    uint16_t len = uart1_pending_len;

    uart1_pending_len = 0u;
    uart1_tx_busy = 1u;
    HAL_UART_Transmit_IT(&huart1, uart1_tx_buf, len);
  }
}

static void usb_queue_send(const uint8_t *data, uint16_t len)
{
  if (len > sizeof(usb_tx_buf))
  {
    return;
  }

  memcpy(usb_tx_buf, data, len);
  usb_pending_len = len;
}

static void usb_try_send_pending(void)
{
  if ((usb_pending_len > 0u) && !usb_tx_busy)
  {
    uint16_t len = usb_pending_len;

    usb_tx_busy = 1u;
    if (CDC_Transmit_FS(usb_tx_buf, len) == USBD_OK)
    {
      usb_pending_len = 0u;
    }
    else
    {
      usb_tx_busy = 0u; /* thu lai o vong lap sau */
    }
  }
}

/* Gui xuong UART1 lenh cap ID=1 cho slave dau tien va bat dau cho ACK 200ms. */
static void uart_send_set_id_first(void)
{
  uint8_t data[3];

  data[0] = P2L_UART_FRAME_SOF;
  data[1] = P2L_UART_CMD_SET_ID;
  data[2] = 1u;
  uart1_queue_send(data, 3u);

  id_ack_deadline = HAL_GetTick() + P2L_UART_ID_ACK_TIMEOUT_MS;
  id_ack_wait_active = 1u;
}

static void id_scan_record_result(uint8_t count)
{
  if (id_scan_last_result_valid && (id_scan_last_result == count))
  {
    id_scan_same_result_count++;
  }
  else
  {
    id_scan_last_result = count;
    id_scan_last_result_valid = 1u;
    id_scan_same_result_count = 1u;
  }

  g_p2l.scanned_slave_count = count;

  if ((count >= P2L_UART_MAX_SLAVES) ||
      (id_scan_attempt_count >= P2L_UART_MAX_SLAVES) ||
      (id_scan_same_result_count >= P2L_SCAN_SAME_RESULT_LIMIT))
  {
    g_p2l.scan_in_progress = 0u;
    usb_send_id_result(count);
  }
  else
  {
    id_scan_attempt_count++;
    uart_send_set_id_first();
  }
}

/* Bao cho PC biet ket qua quet (so slave, 0 neu khong co slave nao phan hoi). */
static void usb_send_id_result(uint8_t count)
{
  uint8_t data[3];

  data[0] = P2L_UART_FRAME_SOF;
  data[1] = P2L_UART_CMD_ID_RESULT;
  data[2] = count;
  usb_queue_send(data, 3u);
}

static void usb_send_slave_count(void)
{
  uint8_t data[3];

  data[0] = P2L_UART_FRAME_SOF;
  data[1] = P2L_UART_CMD_GET_SLAVE_COUNT;
  data[2] = g_p2l.scanned_slave_count;
  usb_queue_send(data, 3u);
}

void P2L_UART_StartIdScan(void)
{
  g_p2l.scan_in_progress = 1u;
  id_scan_attempt_count = 0u;
  id_scan_same_result_count = 0u;
  id_scan_last_result = 0u;
  id_scan_last_result_valid = 0u;
  id_scan_attempt_count++;
  uart_send_set_id_first();
}

void P2L_UART_SendResetQty(void)
{
  uint8_t data[2];

  data[0] = P2L_UART_FRAME_SOF;
  data[1] = P2L_UART_CMD_RESET_QTY;
  uart1_queue_send(data, 2u);
}

void P2L_UART_SendSaveId(void)
{
  uint8_t data[2];

  data[0] = P2L_UART_FRAME_SOF;
  data[1] = P2L_UART_CMD_SAVE_ID;
  uart1_queue_send(data, 2u);
}

/* Xu ly khung nhan tu chuoi slave qua UART1: chi quan tam ACK va ID_RESULT cua qua trinh quet ID. */
static void uart1_process_frame(void)
{
  uint16_t size = uart1_rx_frame_len;
  uint8_t cmd;

  if (size == 0u)
  {
    return;
  }

  uart1_rx_frame_len = 0u;

  if (uart1_rx_frame[0] != P2L_UART_FRAME_SOF)
  {
    return;
  }

  cmd = uart1_rx_frame[1];

  if ((cmd == P2L_UART_CMD_SET_ID_ACK) && (size == 2u))
  {
    /* Slave dau tien da nhan duoc lenh, huy dem gio cho, tiep tuc cho ID_RESULT thuc su. */
    id_ack_wait_active = 0u;
    return;
  }

  if ((cmd == P2L_UART_CMD_ID_RESULT) && (size == 3u) &&
      (uart1_rx_frame[2] >= 1u) && (uart1_rx_frame[2] <= P2L_UART_MAX_SLAVES))
  {
    id_ack_wait_active = 0u;
    if (g_p2l.scan_in_progress)
    {
      id_scan_record_result(uart1_rx_frame[2]);
    }
    return;
  }

  /* Cac khung khac tu UART1 (khong mong doi) duoc bo qua. */
}

/* Xu ly lenh nhan tu PC qua USB CDC, giu nguyen dinh dang khung nhu giao thuc slave. */
static void usb_process_frame(void)
{
  uint16_t size = usb_rx_frame_len;
  uint8_t cmd;

  if (size == 0u)
  {
    return;
  }

  usb_rx_frame_len = 0u;

  if (usb_rx_frame[0] != P2L_UART_FRAME_SOF)
  {
    return;
  }

  cmd = usb_rx_frame[1];

  switch (cmd)
  {
    case P2L_UART_CMD_SET_ID:
      if (size == 3u)
      {
        P2L_UART_StartIdScan();
      }
      break;

    case P2L_UART_CMD_SET_QTY:
    {
      uint8_t slave_count = usb_rx_frame[2];

      if ((slave_count <= P2L_UART_MAX_SLAVES_PER_FRAME) &&
          (size == (uint16_t)(3u + (uint16_t)slave_count * 2u)))
      {
        uart1_queue_send(usb_rx_frame, size);
      }
      break;
    }

    case P2L_UART_CMD_RESET_QTY:
      if (size == 2u)
      {
        P2L_UART_SendResetQty();
      }
      break;

    case P2L_UART_CMD_GET_SLAVE_COUNT:
      if (size == 2u)
      {
        usb_send_slave_count();
      }
      break;

    case P2L_UART_CMD_SCAN_SLAVES:
      if (size == 2u)
      {
        P2L_UART_StartIdScan();
      }
      break;

    case P2L_UART_CMD_SAVE_ID:
      if (size == 2u)
      {
        P2L_UART_SendSaveId();
      }
      break;

    default:
      break;
  }
}

/* Debounce nut BTN_ENTER (active low); nhan nut se kich hoat RESET_QTY xuong chuoi slave. */
static void btn_poll(void)
{
  GPIO_PinState reading = HAL_GPIO_ReadPin(BTN_ENTER_GPIO_Port, BTN_ENTER_Pin);
  uint32_t now = HAL_GetTick();

  if (reading != btn_last_reading)
  {
    btn_last_reading = reading;
    btn_last_reading_tick = now;
  }

  if (((now - btn_last_reading_tick) >= P2L_BTN_DEBOUNCE_MS) && (btn_stable_state != btn_last_reading))
  {
    btn_stable_state = btn_last_reading;

    if (btn_stable_state == GPIO_PIN_RESET)
    {
      P2L_UART_SendResetQty();
    }
  }
}

void P2L_UART_Init(void)
{
  g_p2l.scanned_slave_count = 0u;
  g_p2l.scan_in_progress = 0u;

  uart1_rx_frame_len = 0u;
  usb_rx_frame_len = 0u;
  uart1_pending_len = 0u;
  usb_pending_len = 0u;
  uart1_tx_busy = 0u;
  usb_tx_busy = 0u;
  id_ack_wait_active = 0u;
  id_scan_attempt_count = 0u;
  id_scan_same_result_count = 0u;
  id_scan_last_result = 0u;
  id_scan_last_result_valid = 0u;

  btn_stable_state = HAL_GPIO_ReadPin(BTN_ENTER_GPIO_Port, BTN_ENTER_Pin);
  btn_last_reading = btn_stable_state;
  btn_last_reading_tick = HAL_GetTick();

  /* Quet ID tu dong luc khoi dong dang tam vo hieu hoa.
  startup_scan_tick = HAL_GetTick() + P2L_SCAN_STARTUP_DELAY_MS;
  startup_scan_pending = 1u;
  */
  dummy_send_remaining = P2L_DUMMY_SEND_COUNT;
  dummy_send_next_tick = HAL_GetTick() + P2L_SCAN_STARTUP_DELAY_MS;

  HAL_UARTEx_ReceiveToIdle_IT(&huart1, uart1_rx_buf, sizeof(uart1_rx_buf));
}

void P2L_UART_Task(void)
{
  /* Quet ID tu dong luc khoi dong dang tam vo hieu hoa.
  if (startup_scan_pending && ((int32_t)(HAL_GetTick() - startup_scan_tick) >= 0))
  {
    startup_scan_pending = 0u;
    P2L_UART_StartIdScan();
  }
  */

  if ((dummy_send_remaining > 0u) && ((int32_t)(HAL_GetTick() - dummy_send_next_tick) >= 0))
  {
    uint8_t data[2];

    data[0] = P2L_UART_FRAME_SOF;
    data[1] = P2L_DUMMY_CMD;
    uart1_queue_send(data, 2u);

    dummy_send_remaining--;
    dummy_send_next_tick = HAL_GetTick() + P2L_DUMMY_SEND_PERIOD_MS;
  }

  uart1_try_send_pending();
  usb_try_send_pending();

  uart1_process_frame();
  usb_process_frame();

  if (id_ack_wait_active && ((int32_t)(HAL_GetTick() - id_ack_deadline) >= 0))
  {
    /* Khong co slave nao phan hoi ACK -> ghi nhan ket qua 0 va co the thu lai. */
    id_ack_wait_active = 0u;
    if (g_p2l.scan_in_progress)
    {
      id_scan_record_result(0u);
    }
  }

  btn_poll();
}

void P2L_USB_OnReceive(const uint8_t *buf, uint32_t len)
{
  if ((usb_rx_frame_len == 0u) && (len <= sizeof(usb_rx_frame)))
  {
    memcpy(usb_rx_frame, buf, len);
    usb_rx_frame_len = (uint16_t)len;
  }
}

void P2L_USB_OnTransmitCplt(void)
{
  usb_tx_busy = 0u;
}

/* Callback cua HAL khi UART1 nhan duoc du lieu va duong truyen chuyen sang idle. */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  if (huart == &huart1)
  {
    if ((uart1_rx_frame_len == 0u) && (Size <= sizeof(uart1_rx_frame)))
    {
      memcpy(uart1_rx_frame, uart1_rx_buf, Size);
      uart1_rx_frame_len = Size;
    }

    HAL_UARTEx_ReceiveToIdle_IT(&huart1, uart1_rx_buf, sizeof(uart1_rx_buf));
  }
}

/* Callback cua HAL khi UART1 truyen xong (Transmit_IT), go co dang ban. */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == &huart1)
  {
    uart1_tx_busy = 0u;
  }
}
