#ifndef UART_H
#define UART_H

// The console UART, implemented per arch (arch/<arch>/uart.c).

void uartinit(void);             // set up the UART, early on the boot CPU
void uartputc_sync(int c);       // send one raw byte, polling
int uartgetc(void);              // one received byte, or -1 if none
void uart_enable_rx_irq(void);   // unmask receive interrupts in the UART
void uartintr(void);             // receive interrupt: feed consoleintr()

#endif
