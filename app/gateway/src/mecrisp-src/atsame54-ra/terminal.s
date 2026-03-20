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

@ Terminal interface for same70q21 xplained via usart1 (CDC channel through usb connection)
@ PB4 = TXD
@ PA21= RXD


@ Terminalroutinen
@ Terminal code and initialisations.
@ Porting: Rewrite this !


@ -----------------------------------------------------------------------------
uart_init: @ ( -- )
@ -----------------------------------------------------------------------------

// 		This is infact a complete cpu and usart1 setup

reset_handler:	//disable watchdog
// first do nothing ...
	bx	lr  	

  .ltorg @ Hier werden viele spezielle Hardwarestellenkonstanten gebraucht, schreibe sie gleich !

@ Following code is the same as for STM32F051
.include "../common/terminalhooks.s"

@ -----------------------------------------------------------------------------
  Wortbirne Flag_visible, "serial-emit"
serial_emit: @ ( c -- ) Emit one character
@ -----------------------------------------------------------------------------
   push {lr}

1: bl serial_qemit
   cmp tos, #0
   drop
   beq 1b

   mov r0, tos
   bl mecrisp_emit
   drop

   pop {pc}

@ -----------------------------------------------------------------------------
  Wortbirne Flag_visible, "serial-key"
serial_key: @ ( -- c ) Receive one character
@ -----------------------------------------------------------------------------
   push {lr}

1: bl serial_qkey
   cmp tos, #0
   drop
   beq 1b

   pushdatos
   bl mecrisp_key
   mov tos, r0         @ Fetch the character

   pop {pc}


@ -----------------------------------------------------------------------------
  Wortbirne Flag_visible, "serial-emit?"
serial_qemit:  @ ( -- ? ) Ready to send a character ?
@ -----------------------------------------------------------------------------
   push {lr}
   bl pause

   pushdaconst 0    @ False Flag
   bl mecrisp_qemit
   cbz r0,1f
   mvns tos, tos    @ True Flag
1: pop {pc}


@ -----------------------------------------------------------------------------
  Wortbirne Flag_visible, "serial-key?"
serial_qkey:  @ ( -- ? ) Is there a key press ?
@ -----------------------------------------------------------------------------
   push {lr}
   bl pause

   pushdaconst 0  @ False Flag
   bl mecrisp_qkey
   cbz r0,1f
   mvns tos, tos @ True Flag
1: pop {pc}

  .ltorg @ Hier werden viele spezielle Hardwarestellenkonstanten gebraucht, schreibe sie gleich !

@ -----------------------------------------------------------------------------
  Wortbirne Flag_visible, "forthrestart" @ ( -- ) Restart mecrisp
forthrestart:
@ -----------------------------------------------------------------------------
  bl mecrisp_core

@ -----------------------------------------------------------------------------
  Wortbirne Flag_visible, "#exit" @ ( -- ) exit to zephyr shell
exit_zephyr:
@ -----------------------------------------------------------------------------
  push {lr}
  bl stop_shell_bypass_forth
  pop {pc}

