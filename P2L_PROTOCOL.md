# PickToLightMaster - Huong dan lenh va giao thuc

Tai lieu nay mo ta cac lenh ma PC gui qua USB CDC cho board Master, cac lenh Master gui qua UART1 cho chuoi Slave, tham so va ket qua tra ve. Noi dung duoc dong bo voi code trong `Core/Inc/p2l_common.h` va `Core/Src/p2l_uart.c`.

## 1. Mo hinh ket noi

```text
PC <-> USB CDC <-> Master <-> UART1 <-> Slave 1 <-> Slave 2 <-> ... <-> Slave N
```

- PC chi giao tiep voi Master qua USB CDC.
- Master gui lenh dieu khien chuoi Slave qua UART1.
- Cac Slave noi tiep voi nhau thanh mot chuoi.
- Khung du lieu khong co CRC va khong co truong dia chi nguon/dich.
- Moi khung bat dau bang byte `0xA5`.

## 2. Dinh dang khung co ban

| Vi tri | Ten | Y nghia |
|---|---|---|
| Byte 0 | SOF | Luon la `0xA5` |
| Byte 1 | CMD | Ma lenh |
| Byte 2 tro di | DATA | Tham so cua lenh, neu co |

Cac gia tri dang dung (dinh nghia trong `p2l_common.h`):

| Ten trong code | Gia tri | Ghi chu |
|---|---:|---|
| `P2L_UART_FRAME_SOF` | `0xA5` | Tat ca khung |
| `P2L_UART_MAX_SLAVES` | `24` | ID Slave lon nhat va so Slave toi da |
| `P2L_UART_MAX_SLAVES_PER_FRAME` | `24` | So cap ID/Value toi da trong lenh SET_QTY |
| `P2L_UART_ID_ACK_TIMEOUT_MS` | `1800 ms` | Thoi gian cho ACK khi quet ID |

## 3. Bang ma lenh (CMD)

| Ten trong code | Gia tri | Kenh | Chieu di |
|---|---:|---|---|
| `P2L_UART_CMD_SET_ID` | `0x02` | UART1 | Master -> Slave |
| `P2L_UART_CMD_SET_QTY` | `0x03` | USB + UART1 | PC -> Master -> Slave |
| `P2L_UART_CMD_ID_RESULT` | `0x04` | UART1 + USB | Slave -> Master -> PC |
| `P2L_UART_CMD_SET_ID_ACK` | `0x05` | UART1 | Slave -> Master |
| `P2L_UART_CMD_RESET_QTY` | `0x06` | USB + UART1 | PC -> Master -> Slave |
| `P2L_UART_CMD_SAVE_ID` | `0x07` | USB + UART1 | PC -> Master -> Slave |
| `P2L_UART_CMD_SCAN_SLAVES` | `0x08` | USB | PC -> Master |
| `P2L_UART_CMD_GET_SLAVE_COUNT` | `0x09` | USB | PC -> Master |

Luu y: `0x02`-`0x06` la giao thuc UART goc dung chung voi Slave. `0x07`-`0x09` la lenh rieng cua Master, chi trao doi qua USB CDC voi PC (khong xuat hien tren day UART, tru khi Master chu dong phat sinh khung UART tuong ung nhu SAVE_ID va RESET_QTY).

## 4. Lenh gui qua USB CDC (PC -> Master)

### 4.1. Quet Slave: `SCAN_SLAVES = 0x08`

Khung gui:

```text
A5 08
```

Chuc nang:

- Bat dau quet va gan lai ID cho chuoi Slave.
- Master gui lenh SET_ID voi ID bat dau la 1 xuong UART1.
- Master cho Slave phan hoi ACK va ID_RESULT.
- Neu ket qua chua dung dieu kien dung, Master tu dong gui lai lan quet tiep theo.

Dieu kien dung quet:

1. So Slave quet duoc bang `P2L_UART_MAX_SLAVES` (`24`).
2. Da thu toi da `24` lan (`P2L_UART_MAX_SLAVES` lan).
3. Ket qua quet giong nhau `5` lan lien tiep (`P2L_SCAN_SAME_RESULT_LIMIT`).

Neu khong nhan duoc ACK trong `1800 ms` (`P2L_UART_ID_ACK_TIMEOUT_MS`), lan quet do duoc tinh la ket qua `0` va van tinh vao dieu kien dung so 3.

Ket qua cuoi cung tra ve PC:

```text
A5 04 COUNT
```

`COUNT` la so Slave quet duoc, tu `0` den `24`.

Lenh `A5 02 ...` (SET_ID) gui truc tiep tu USB **khong** duoc Master xu ly nhu mot lenh dieu khien; muon quet lai chuoi Slave phai dung `A5 08`.

### 4.2. Doc so Slave da quet: `GET_SLAVE_COUNT = 0x09`

Khung gui:

```text
A5 09
```

Master tra ve:

```text
A5 09 COUNT
```

`COUNT` la ket qua quet gan nhat dang luu trong Master (`g_p2l.scanned_slave_count`). Lenh nay khong quet lai, chi doc gia tri da co san.

### 4.3. Yeu cau Slave luu ID: `SAVE_ID = 0x07`

Khung gui:

```text
A5 07
```

Master chuyen thanh khung broadcast 2 byte va gui xuong UART1 cho chuoi Slave:

```text
A5 07
```

Khong co phan hoi rieng ve USB cho lenh nay.

### 4.4. Dat gia tri hien thi: `SET_QTY = 0x03`

Khung gui:

```text
A5 03 LEN ID1 VAL1 ID2 VAL2 ...
```

Trong do:

- `LEN`: so cap `ID/VAL`, tu `0` den `24` (`P2L_UART_MAX_SLAVES_PER_FRAME`).
- `IDx`: ID cua Slave, so nguyen tu `1` den `24`.
- `VALx`: gia tri hien thi, theo giao thuc Slave la tu `0` den `99`.
- Do dai khung phai dung `3 + LEN * 2` byte, neu khong Master se bo qua khung.

Vi du dat Slave 1 hien thi 12 va Slave 3 hien thi 5:

```text
A5 03 02 01 0C 03 05
```

Master kiem tra do dai khung, sau do chuyen nguyen khung xuong UART1 (khong sua doi noi dung).

### 4.5. Xoa gia tri hien thi: `RESET_QTY = 0x06`

Khung gui:

```text
A5 06
```

Master chuyen lenh nay xuong UART1. Tat ca Slave nhan lenh se dat gia tri hien thi ve 0, khong phu thuoc ID. Lenh nay cung duoc kich hoat tu dong khi nhan BTN_ENTER tren board Master.

## 5. Lenh tren UART1 giua Master va Slave

### 5.1. Gan ID theo chuoi: `SET_ID = 0x02`

Master gui cho Slave dau tien (ID luon bat dau tu 1):

```text
A5 02 01
```

Cach hoat dong:

1. Slave dau tien nhan ID `1`.
2. Slave gui ACK nguoc ve Master:

   ```text
   A5 05
   ```

3. Neu phia sau con Slave, Slave hien tai chuyen tiep lenh voi ID tang len (vi du `A5 02 02`).
4. Qua trinh tiep tuc cho den khi het chuoi.
5. Slave cuoi gui ket qua ve Master:

   ```text
   A5 04 LAST_ID
   ```

`LAST_ID` chinh la tong so Slave tim thay trong lan quet do.

### 5.2. ACK gan ID: `SET_ID_ACK = 0x05`

Khung:

```text
A5 05
```

Y nghia: Slave dau tien da nhan duoc lenh SET_ID. ACK nay chi xac nhan viec nhan lenh; Master van cho ID_RESULT de biet tong so Slave.

### 5.3. Ket qua quet: `ID_RESULT = 0x04`

Khung:

```text
A5 04 LAST_ID
```

- Duoc Slave cuoi gui nguoc ve Master.
- Cac Slave trung gian chuyen tiep nguyen khung.
- `LAST_ID` la so Slave trong chuoi.
- Master dung khung nay lam ket qua cua mot lan quet (xem muc 4.1).

### 5.4. Dat gia tri hien thi: `SET_QTY = 0x03`

Master chuyen tiep nguyen khung SET_QTY nhan tu USB xuong UART1. Moi Slave kiem tra cac cap ID/Value, neu ID trung voi minh thi cap nhat gia tri, sau do van chuyen tiep khung xuong Slave ke tiep.

### 5.5. Xoa gia tri hien thi: `RESET_QTY = 0x06`

Khung:

```text
A5 06
```

Day la lenh broadcast. Moi Slave dat gia tri hien thi ve 0 va chuyen tiep lenh xuong Slave ke tiep.

### 5.6. Luu ID: `SAVE_ID = 0x07`

Khung:

```text
A5 07
```

Master gui khung nay xuong UART1 khi nhan lenh `SAVE_ID` tu USB CDC (xem muc 4.3). Day la lenh broadcast, khong co tham so.

## 6. Trinh tu quet ID (chi tiet)

Khi PC gui `A5 08`, Master thuc hien:

```text
PC -> Master: A5 08
Master -> Slave chain: A5 02 01
Slave chain -> Master: A5 05
Slave chain -> Master: A5 04 COUNT
Master -> PC: A5 04 COUNT   (chi gui khi da dat dieu kien dung)
```

Neu ket qua chua dung dieu kien dung (muc 4.1), Master lap lai buoc gui `A5 02 01` va tiep tuc cho phan hoi, khong gui `A5 04` trung gian ve PC.

Trong luc dang quet, khong nen gui them lenh `A5 08` moi: lenh moi se khoi tao lai toan bo bo dem cua qua trinh quet hien tai (so lan thu, so ket qua giong nhau lien tiep).

## 7. Che do quet ID tu dong luc khoi dong (DA TAT)

Truoc day Master tu dong gui `A5 08` khoang 1 giay sau khi cap nguon. Hien tai tinh nang nay **da bi vo hieu hoa** (code lien quan bi comment lai trong `P2L_UART_Init()` va `P2L_UART_Task()`).

Thay vao do, sau khi khoi dong khoang 1 giay, Master gui mot chuoi khung dummy xuong UART1 de kiem tra duong truyen:

```text
A5 09
```

Dac diem cua khung dummy nay:

- Gui `24` lan lien tiep (`P2L_DUMMY_SEND_COUNT`), moi lan cach nhau `50 ms` (`P2L_DUMMY_SEND_PERIOD_MS`).
- Day la khung UART1 thuan tuy, khong lien quan den lenh USB `GET_SLAVE_COUNT` (cung co byte CMD la `0x09` nhung khac kenh: mot ben la UART1 gui xuong Slave, mot ben la USB CDC trao doi voi PC).
- Slave chua dinh nghia xu ly cho CMD `0x09` tren UART, nen khung nay hien chi dung de do dac/kiem tra tin hieu, khong tao hieu ung chuc nang.
- Khong anh huong den `g_p2l.scanned_slave_count` hay trang thai quet ID.

Muon quet ID that su, phai chu dong gui `A5 08` qua USB CDC (xem muc 4.1).

## 8. Bang tom tat nhanh

| Lenh | Gui qua | Khung | Ket qua / chuc nang |
|---|---|---|---|
| `SCAN_SLAVES` `0x08` | USB -> Master | `A5 08` | Quet lai chuoi Slave (lap lai toi da 24 lan) |
| `GET_SLAVE_COUNT` `0x09` | USB -> Master | `A5 09` | Tra ve `A5 09 COUNT` |
| `SAVE_ID` `0x07` | USB -> Master -> UART1 | `A5 07` | Yeu cau Slave luu ID vao bo nho |
| `SET_QTY` `0x03` | USB -> Master -> UART1 | `A5 03 LEN ...` | Dat gia tri cho cac Slave |
| `RESET_QTY` `0x06` | USB -> Master -> UART1 | `A5 06` | Xoa gia tri tat ca Slave |
| `SET_ID` `0x02` | Master -> Slave | `A5 02 ID` | Gan ID cho tung Slave trong chuoi |
| `SET_ID_ACK` `0x05` | Slave -> Master | `A5 05` | Xac nhan da nhan SET_ID |
| `ID_RESULT` `0x04` | Slave -> Master -> USB | `A5 04 COUNT` | Bao so Slave tim thay |

## 9. Vi du su dung tuong tac

### Quet chuoi Slave

PC gui:

```text
A5 08
```

Master tu dong gui nhieu lan xuong UART1:

```text
A5 02 01
```

Gia su ket qua cuoi cung la 4 Slave, PC nhan:

```text
A5 04 04
```

### Yeu cau Slave luu ID

```text
A5 07
```

### Dat hien thi cho hai Slave

Dat Slave 1 la 25 va Slave 2 la 7:

```text
A5 03 02 01 19 02 07
```

### Xoa toan bo hien thi

```text
A5 06
```

### Doc lai so Slave da quet

PC gui:

```text
A5 09
```

PC nhan:

```text
A5 09 04
```
