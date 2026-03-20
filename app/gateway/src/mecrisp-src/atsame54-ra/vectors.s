@
@    Mecrisp-Stellaris - A native code Forth implementation for ARM-Cortex M microcontrollers
@    Copyright (C) 2013  Matthias Koch
@
@    This program is free software: you can redistribute it and/or modify
@    it under the terms of the GNU General Public License as published by
@    the Free Software Foundation, either version 3 of the License, or
@    (at your option) any later version.
@
@    This program is distributed in the hope that it will be useful,
@    but WITHOUT ANY WARRANTY; without even the implied warranty of
@    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
@    GNU General Public License for more details.
@
@    You should have received a copy of the GNU General Public License
@    along with this program.  If not, see <http://www.gnu.org/licenses/>.
@

@ -----------------------------------------------------------------------------
@ Interruptvektortabelle
@ -----------------------------------------------------------------------------

.include "../common/vectors-common.s"

@ Special interrupt handlers for this particular chip:

.word nullhandler+1 @ Position  0: PM - Power Manager
.word nullhandler+1 @ Position  1: MCLK - Main Clock
.word nullhandler+1 @ Position  2: OSCCTRL - Oscillators Control
.word nullhandler+1 @ Position  3: OSCCTRL - Oscillators Control
.word nullhandler+1 @ Position  4: OSCCTRL - Oscillators Control
.word nullhandler+1 @ Position  5: OSCCTRL - Oscillators Control
.word nullhandler+1 @ Position  6: OSCCTRL - Oscillators Control
.word nullhandler+1 @ Position  7: OSC32KCTRL - 32 kHz Oscillators Control
.word nullhandler+1 @ Position  8: SUPC - Supply Controller
.word nullhandler+1 @ Position  9: SUPC - Supply Controller
.word nullhandler+1 @ Position 10: WDT - Watchdog Timer
.word nullhandler+1 @ Position 11: RTC - Real-Time Counter
.word nullhandler+1 @ Position 12: EIC - External Interrupt Controller EXTINT 0
.word nullhandler+1 @ Position 13: EIC - External Interrupt Controller EXTINT 1
.word nullhandler+1 @ Position 14: EIC - External Interrupt Controller EXTINT 2
.word nullhandler+1 @ Position 15: EIC - External Interrupt Controller EXTINT 3
.word nullhandler+1 @ Position 16: EIC - External Interrupt Controller EXTINT 4
.word nullhandler+1 @ Position 17: EIC - External Interrupt Controller EXTINT 5
.word nullhandler+1 @ Position 18: EIC - External Interrupt Controller EXTINT 6
.word nullhandler+1 @ Position 19: EIC - External Interrupt Controller EXTINT 7
.word nullhandler+1 @ Position 20: EIC - External Interrupt Controller EXTINT 8
.word nullhandler+1 @ Position 21: EIC - External Interrupt Controller EXTINT 9
.word nullhandler+1 @ Position 22: EIC - External Interrupt Controller EXTINT 10
.word nullhandler+1 @ Position 23: EIC - External Interrupt Controller EXTINT 11
.word nullhandler+1 @ Position 24: EIC - External Interrupt Controller EXTINT 12
.word nullhandler+1 @ Position 25: EIC - External Interrupt Controller EXTINT 13
.word nullhandler+1 @ Position 26: EIC - External Interrupt Controller EXTINT 14
.word nullhandler+1 @ Position 27: EIC - External Interrupt Controller EXTINT 15
.word nullhandler+1 @ Position 28: FREQM - Frequency Meter
.word nullhandler+1 @ Position 29: NVMCTRL - Non-Volatile Memory Controller
.word nullhandler+1 @ Position 30: NVMCTRL - Non-Volatile Memory Controller
.word nullhandler+1 @ Position 31: DMAC - Direct Memory Access Controller
.word nullhandler+1 @ Position 32: DMAC - Direct Memory Access Controller
.word nullhandler+1 @ Position 33: DMAC - Direct Memory Access Controller
.word nullhandler+1 @ Position 34: DMAC - Direct Memory Access Controller
.word nullhandler+1 @ Position 35: DMAC - Direct Memory Access Controller
.word nullhandler+1 @ Position 36: EVSYS - Event System Interface
.word nullhandler+1 @ Position 37: EVSYS - Event System Interface
.word nullhandler+1 @ Position 38: EVSYS - Event System Interface
.word nullhandler+1 @ Position 39: EVSYS - Event System Interface
.word nullhandler+1 @ Position 40: EVSYS - Event System Interface
.word nullhandler+1 @ Position 41: PAC - Peripheral Access Controller
.word nullhandler+1 @ Position 42: noop
.word nullhandler+1 @ Position 43: noop
.word nullhandler+1 @ Position 44: noop
.word nullhandler+1 @ Position 45: RAM ECC
.word nullhandler+1 @ Position 46: SERCOM0 - Serial Communication Interface 0
.word nullhandler+1 @ Position 47: SERCOM0 - Serial Communication Interface 0
.word nullhandler+1 @ Position 48: SERCOM0 - Serial Communication Interface 0
.word nullhandler+1 @ Position 49: SERCOM0 - Serial Communication Interface 0
.word nullhandler+1 @ Position 50: SERCOM1 - Serial Communication Interface 1
.word nullhandler+1 @ Position 51: SERCOM1 - Serial Communication Interface 1
.word nullhandler+1 @ Position 52: SERCOM1 - Serial Communication Interface 1
.word nullhandler+1 @ Position 53: SERCOM1 - Serial Communication Interface 1
.word nullhandler+1 @ Position 54: SERCOM2 - Serial Communication Interface 2
.word nullhandler+1 @ Position 55: SERCOM2 - Serial Communication Interface 2
.word nullhandler+1 @ Position 56: SERCOM2 - Serial Communication Interface 2
.word nullhandler+1 @ Position 57: SERCOM2 - Serial Communication Interface 2
.word nullhandler+1 @ Position 58: SERCOM3 - Serial Communication Interface 3
.word nullhandler+1 @ Position 59: SERCOM3 - Serial Communication Interface 3
.word nullhandler+1 @ Position 60: SERCOM3 - Serial Communication Interface 3
.word nullhandler+1 @ Position 61: SERCOM3 - Serial Communication Interface 3
.word nullhandler+1 @ Position 62: SERCOM4 - Serial Communication Interface 4
.word nullhandler+1 @ Position 63: SERCOM4 - Serial Communication Interface 4
.word nullhandler+1 @ Position 64: SERCOM4 - Serial Communication Interface 4
.word nullhandler+1 @ Position 65: SERCOM4 - Serial Communication Interface 4
.word nullhandler+1 @ Position 66: SERCOM5 - Serial Communication Interface 5
.word nullhandler+1 @ Position 67: SERCOM5 - Serial Communication Interface 5
.word nullhandler+1 @ Position 68: SERCOM5 - Serial Communication Interface 5
.word nullhandler+1 @ Position 69: SERCOM5 - Serial Communication Interface 5
.word nullhandler+1 @ Position 70: SERCOM6 - Serial Communication Interface 6
.word nullhandler+1 @ Position 71: SERCOM6 - Serial Communication Interface 6
.word nullhandler+1 @ Position 72: SERCOM6 - Serial Communication Interface 6
.word nullhandler+1 @ Position 73: SERCOM6 - Serial Communication Interface 6
.word nullhandler+1 @ Position 74: SERCOM7 - Serial Communication Interface 7
.word nullhandler+1 @ Position 75: SERCOM7 - Serial Communication Interface 7
.word nullhandler+1 @ Position 76: SERCOM7 - Serial Communication Interface 7
.word nullhandler+1 @ Position 77: SERCOM7 - Serial Communication Interface 7
.word nullhandler+1 @ Position 78: CAN0 - Control Area Network 0
.word nullhandler+1 @ Position 79: CAN1 - Control Area Network 1
.word nullhandler+1 @ Position 80: USB - Universal Serial Bus
.word nullhandler+1 @ Position 81: USB - Universal Serial Bus
.word nullhandler+1 @ Position 82: USB - Universal Serial Bus
.word nullhandler+1 @ Position 83: USB - Universal Serial Bus
.word nullhandler+1 @ Position 84: GMAC - Ethernet MAC
.word nullhandler+1 @ Position 85: TCC0 - Timer Counter Control 0
.word nullhandler+1 @ Position 86: TCC0 - Timer Counter Control 0
.word nullhandler+1 @ Position 87: TCC0 - Timer Counter Control 0
.word nullhandler+1 @ Position 88: TCC0 - Timer Counter Control 0
.word nullhandler+1 @ Position 89: TCC0 - Timer Counter Control 0
.word nullhandler+1 @ Position 90: TCC0 - Timer Counter Control 0
.word nullhandler+1 @ Position 91: TCC0 - Timer Counter Control 0
.word nullhandler+1 @ Position 92: TCC1 - Timer Counter Control 1
.word nullhandler+1 @ Position 93: TCC1 - Timer Counter Control 1
.word nullhandler+1 @ Position 94: TCC1 - Timer Counter Control 1
.word nullhandler+1 @ Position 95: TCC1 - Timer Counter Control 1
.word nullhandler+1 @ Position 96: TCC1 - Timer Counter Control 1
.word nullhandler+1 @ Position 97: TCC2 - Timer Counter Control 2
.word nullhandler+1 @ Position 98: TCC2 - Timer Counter Control 2
.word nullhandler+1 @ Position 99: TCC2 - Timer Counter Control 2
.word nullhandler+1 @ Position 100: TCC2 - Timer Counter Control 2
.word nullhandler+1 @ Position 101: TCC3 - Timer Counter Control 3
.word nullhandler+1 @ Position 102: TCC3 - Timer Counter Control 3
.word nullhandler+1 @ Position 103: TCC3 - Timer Counter Control 3
.word nullhandler+1 @ Position 104: TCC4 - Timer Counter Control 4
.word nullhandler+1 @ Position 105: TCC4 - Timer Counter Control 4
.word nullhandler+1 @ Position 106: TCC4 - Timer Counter Control 4
.word nullhandler+1 @ Position 107: TC0 - Basic Timer Counter 0
.word nullhandler+1 @ Position 108: TC1 - Basic Timer Counter 1
.word nullhandler+1 @ Position 109: TC2 - Basic Timer Counter 2
.word nullhandler+1 @ Position 110: TC3 - Basic Timer Counter 3
.word nullhandler+1 @ Position 111: TC4 - Basic Timer Counter 4
.word nullhandler+1 @ Position 112: TC5 - Basic Timer Counter 5
.word nullhandler+1 @ Position 113: TC6 - Basic Timer Counter 6
.word nullhandler+1 @ Position 114: TC7 - Basic Timer Counter 7
.word nullhandler+1 @ Position 115: PDEC - Position Decoder
.word nullhandler+1 @ Position 116: PDEC - Position Decoder
.word nullhandler+1 @ Position 117: PDEC - Position Decoder
.word nullhandler+1 @ Position 118: ADC0 - Analog Digital Converter 0
.word nullhandler+1 @ Position 119: ADC0 - Analog Digital Converter 0
.word nullhandler+1 @ Position 120: ADC1 - Analog Digital Converter 1
.word nullhandler+1 @ Position 121: ADC1 - Analog Digital Converter 1
.word nullhandler+1 @ Position 122: AC - Analog Comparators
.word nullhandler+1 @ Position 123: DAC - Digital-to-Analog Converter
.word nullhandler+1 @ Position 124: DAC - Digital-to-Analog Converter
.word nullhandler+1 @ Position 125: DAC - Digital-to-Analog Converter
.word nullhandler+1 @ Position 126: DAC - Digital-to-Analog Converter
.word nullhandler+1 @ Position 127: DAC - Digital-to-Analog Converter
.word nullhandler+1 @ Position 128: I2S - Inter-IC Sound Interface
.word nullhandler+1 @ Position 129: PCC - Parallel Capture Controller
.word nullhandler+1 @ Position 130: AES - Advanced Encryption Standard
.word nullhandler+1 @ Position 131: TRNG - True Random Generator
.word nullhandler+1 @ Position 132: ICM - Integrity Check Monitor
.word nullhandler+1 @ Position 133: Reserved
.word nullhandler+1 @ Position 134: QSPI - Quad SPI interface
.word nullhandler+1 @ Position 135: SDHC0 - SD/MMC Host Controller 0
.word nullhandler+1 @ Position 136: SDHC1 - SD/MMC Host Controller 1

@ -----------------------------------------------------------------------------
