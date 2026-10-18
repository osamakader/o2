// 16550a UART: the console on qemu virt (riscv64). After xv6.

#include "types.h"
#include "memlayout.h"
#include "uart.h"
#include "console.h"
#include "board.h"

// the UART control registers are memory-mapped at board.uart.
#define Reg(reg) ((volatile unsigned char *)(P2V(board.uart) + (reg)))

// the UART control registers.
// some have different meanings for read vs write.
// see http://byterunner.com/16550.html
#define RHR 0                 // receive holding register (for input bytes)
#define THR 0                 // transmit holding register (for output bytes)
#define IER 1                 // interrupt enable register
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2                 // FIFO control register
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) // clear the content of the two FIFOs
#define ISR 2                 // interrupt status register
#define LCR 3                 // line control register
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate
#define LSR 5                 // line status register
#define LSR_RX_READY (1<<0)   // input is waiting to be read from RHR
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

void uartinit(void)
{
  // disable interrupts.
  WriteReg(IER, 0x00);

  // special mode to set baud rate.
  WriteReg(LCR, LCR_BAUD_LATCH);

  // LSB for baud rate of 38.4K.
  WriteReg(0, 0x03);

  // MSB for baud rate of 38.4K.
  WriteReg(1, 0x00);

  // leave set-baud mode,
  // and set word length to 8 bits, no parity.
  WriteReg(LCR, LCR_EIGHT_BITS);

  // reset and enable FIFOs.
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);
}

// Send one byte, polling until the transmit holding register is free.
void uartputc_sync(int c)
{
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);
}

// Read one input character, or -1 if none is waiting.
int uartgetc(void)
{
  if(ReadReg(LSR) & LSR_RX_READY)
    return ReadReg(RHR);
  return -1;
}

// Enable the receive interrupt. The UART's PLIC source must also be
// enabled.
void uart_enable_rx_irq(void)
{
  WriteReg(IER, IER_RX_ENABLE);
}

// UART interrupt: drain the receive FIFO into the console.
void uartintr(void)
{
  int c;

  while((c = uartgetc()) >= 0)
    consoleintr(c);
}
