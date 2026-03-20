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

@ Schreiben und Löschen des Flash-Speichers.
@ Write and erase of flash memory.

@=========================== NVMCTRL ===========================@
.equ NVMCTRL_BASE, 0x41004000 @ (Non-Volatile Memory Controller) 
    .equ NVMCTRL_CTRLA, NVMCTRL_BASE + 0x0 @ (Control A) 
        .equ NVMCTRL_AUTOWS_Shift, 2   @ bitWidth 1 (Auto Wait State Enable)  
        .equ NVMCTRL_SUSPEN_Shift, 3   @ bitWidth 1 (Suspend Enable)  
        .equ NVMCTRL_WMODE_Shift, 4   @ bitWidth 2 (Write Mode)  
        .equ NVMCTRL_PRM_Shift, 6   @ bitWidth 2 (Power Reduction Mode during Sleep)  
        .equ NVMCTRL_RWS_Shift, 8   @ bitWidth 4 (NVM Read Wait States)  
        .equ NVMCTRL_AHBNS0_Shift, 12   @ bitWidth 1 (Force AHB0 access to NONSEQ, burst transfers are continuously rearbitrated)  
        .equ NVMCTRL_AHBNS1_Shift, 13   @ bitWidth 1 (Force AHB1 access to NONSEQ, burst transfers are continuously rearbitrated)  
        .equ NVMCTRL_CACHEDIS0_Shift, 14   @ bitWidth 1 (AHB0 Cache Disable)  
        .equ NVMCTRL_CACHEDIS1_Shift, 15   @ bitWidth 1 (AHB1 Cache Disable)  
 
    .equ NVMCTRL_CTRLB, NVMCTRL_BASE + 0x4 @ (Control B) 
        .equ NVMCTRL_CMD_Shift, 0   @ bitWidth 7 (Command)  
        .equ NVMCTRL_CMDEX_Shift, 8   @ bitWidth 8 (Command Execution)  
 
    .equ NVMCTRL_PARAM, NVMCTRL_BASE + 0x8 @ (NVM Parameter) 
        .equ NVMCTRL_NVMP_Shift, 0   @ bitWidth 16 (NVM Pages)  
        .equ NVMCTRL_PSZ_Shift, 16   @ bitWidth 3 (Page Size)  
        .equ NVMCTRL_SEE_Shift, 31   @ bitWidth 1 (SmartEEPROM Supported)  
 
    .equ NVMCTRL_INTENCLR, NVMCTRL_BASE + 0xC @ (Interrupt Enable Clear) 
        .equ NVMCTRL_DONE_Shift, 0   @ bitWidth 1 (Command Done Interrupt Clear)  
        .equ NVMCTRL_ADDRE_Shift, 1   @ bitWidth 1 (Address Error)  
        .equ NVMCTRL_PROGE_Shift, 2   @ bitWidth 1 (Programming Error Interrupt Clear)  
        .equ NVMCTRL_LOCKE_Shift, 3   @ bitWidth 1 (Lock Error Interrupt Clear)  
        .equ NVMCTRL_ECCSE_Shift, 4   @ bitWidth 1 (ECC Single Error Interrupt Clear)  
        .equ NVMCTRL_ECCDE_Shift, 5   @ bitWidth 1 (ECC Dual Error Interrupt Clear)  
        .equ NVMCTRL_NVME_Shift, 6   @ bitWidth 1 (NVM Error Interrupt Clear)  
        .equ NVMCTRL_SUSP_Shift, 7   @ bitWidth 1 (Suspended Write Or Erase Interrupt Clear)  
        .equ NVMCTRL_SEESFULL_Shift, 8   @ bitWidth 1 (Active SEES Full Interrupt Clear)  
        .equ NVMCTRL_SEESOVF_Shift, 9   @ bitWidth 1 (Active SEES Overflow Interrupt Clear)  
        .equ NVMCTRL_SEEWRC_Shift, 10   @ bitWidth 1 (SEE Write Completed Interrupt Clear)  
 
    .equ NVMCTRL_INTENSET, NVMCTRL_BASE + 0xE @ (Interrupt Enable Set) 
        .equ NVMCTRL_DONE_Shift, 0   @ bitWidth 1 (Command Done Interrupt Enable)  
        .equ NVMCTRL_ADDRE_Shift, 1   @ bitWidth 1 (Address Error Interrupt Enable)  
        .equ NVMCTRL_PROGE_Shift, 2   @ bitWidth 1 (Programming Error Interrupt Enable)  
        .equ NVMCTRL_LOCKE_Shift, 3   @ bitWidth 1 (Lock Error Interrupt Enable)  
        .equ NVMCTRL_ECCSE_Shift, 4   @ bitWidth 1 (ECC Single Error Interrupt Enable)  
        .equ NVMCTRL_ECCDE_Shift, 5   @ bitWidth 1 (ECC Dual Error Interrupt Enable)  
        .equ NVMCTRL_NVME_Shift, 6   @ bitWidth 1 (NVM Error Interrupt Enable)  
        .equ NVMCTRL_SUSP_Shift, 7   @ bitWidth 1 (Suspended Write Or Erase Interrupt Enable)  
        .equ NVMCTRL_SEESFULL_Shift, 8   @ bitWidth 1 (Active SEES Full Interrupt Enable)  
        .equ NVMCTRL_SEESOVF_Shift, 9   @ bitWidth 1 (Active SEES Overflow Interrupt Enable)  
        .equ NVMCTRL_SEEWRC_Shift, 10   @ bitWidth 1 (SEE Write Completed Interrupt Enable)  
 
    .equ NVMCTRL_INTFLAG, NVMCTRL_BASE + 0x10 @ (Interrupt Flag Status and Clear) 
        .equ NVMCTRL_DONE_Shift, 0   @ bitWidth 1 (Command Done)  
        .equ NVMCTRL_ADDRE_Shift, 1   @ bitWidth 1 (Address Error)  
        .equ NVMCTRL_PROGE_Shift, 2   @ bitWidth 1 (Programming Error)  
        .equ NVMCTRL_LOCKE_Shift, 3   @ bitWidth 1 (Lock Error)  
        .equ NVMCTRL_ECCSE_Shift, 4   @ bitWidth 1 (ECC Single Error)  
        .equ NVMCTRL_ECCDE_Shift, 5   @ bitWidth 1 (ECC Dual Error)  
        .equ NVMCTRL_NVME_Shift, 6   @ bitWidth 1 (NVM Error)  
        .equ NVMCTRL_SUSP_Shift, 7   @ bitWidth 1 (Suspended Write Or Erase Operation)  
        .equ NVMCTRL_SEESFULL_Shift, 8   @ bitWidth 1 (Active SEES Full)  
        .equ NVMCTRL_SEESOVF_Shift, 9   @ bitWidth 1 (Active SEES Overflow)  
        .equ NVMCTRL_SEEWRC_Shift, 10   @ bitWidth 1 (SEE Write Completed)  
 
    .equ NVMCTRL_STATUS, NVMCTRL_BASE + 0x12 @ (Status) 
        .equ NVMCTRL_READY_Shift, 0   @ bitWidth 1 (Ready to accept a command)  
        .equ NVMCTRL_PRM_Shift, 1   @ bitWidth 1 (Power Reduction Mode)  
        .equ NVMCTRL_LOAD_Shift, 2   @ bitWidth 1 (NVM Page Buffer Active Loading)  
        .equ NVMCTRL_SUSP_Shift, 3   @ bitWidth 1 (NVM Write Or Erase Operation Is Suspended)  
        .equ NVMCTRL_AFIRST_Shift, 4   @ bitWidth 1 (BANKA First)  
        .equ NVMCTRL_BPDIS_Shift, 5   @ bitWidth 1 (Boot Loader Protection Disable)  
        .equ NVMCTRL_BOOTPROT_Shift, 8   @ bitWidth 4 (Boot Loader Protection Size)  
 
    .equ NVMCTRL_ADDR, NVMCTRL_BASE + 0x14 @ (Address) 
        .equ NVMCTRL_ADDR_Shift, 0   @ bitWidth 24 (NVM Address)  
 
    .equ NVMCTRL_RUNLOCK, NVMCTRL_BASE + 0x18 @ (Lock Section) 
        .equ NVMCTRL_RUNLOCK_Shift, 0   @ bitWidth 32 (Region Un-Lock Bits)  
 
    .equ NVMCTRL_ECCERR, NVMCTRL_BASE + 0x24 @ (ECC Error Status Register) 
        .equ NVMCTRL_ADDR_Shift, 0   @ bitWidth 24 (Error Address)  
        .equ NVMCTRL_TYPEL_Shift, 28   @ bitWidth 2 (Low Double-Word Error Type)  
        .equ NVMCTRL_TYPEH_Shift, 30   @ bitWidth 2 (High Double-Word Error Type)  
 
    .equ NVMCTRL_DBGCTRL, NVMCTRL_BASE + 0x28 @ (Debug Control) 
        .equ NVMCTRL_ECCDIS_Shift, 0   @ bitWidth 1 (Debugger ECC Read Disable)  
        .equ NVMCTRL_ECCELOG_Shift, 1   @ bitWidth 1 (Debugger ECC Error Tracking Mode)  
 
    .equ NVMCTRL_SEECFG, NVMCTRL_BASE + 0x2A @ (SmartEEPROM Configuration Register) 
        .equ NVMCTRL_WMODE_Shift, 0   @ bitWidth 1 (Write Mode)  
        .equ NVMCTRL_APRDIS_Shift, 1   @ bitWidth 1 (Automatic Page Reallocation Disable)  
 
    .equ NVMCTRL_SEESTAT, NVMCTRL_BASE + 0x2C @ (SmartEEPROM Status Register) 
        .equ NVMCTRL_ASEES_Shift, 0   @ bitWidth 1 (Active SmartEEPROM Sector)  
        .equ NVMCTRL_LOAD_Shift, 1   @ bitWidth 1 (Page Buffer Loaded)  
        .equ NVMCTRL_BUSY_Shift, 2   @ bitWidth 1 (Busy)  
        .equ NVMCTRL_LOCK_Shift, 3   @ bitWidth 1 (SmartEEPROM Write Access Is Locked)  
        .equ NVMCTRL_RLOCK_Shift, 4   @ bitWidth 1 (SmartEEPROM Write Access To Register Address Space Is Locked)  
        .equ NVMCTRL_SBLK_Shift, 8   @ bitWidth 4 (Blocks Number In a Sector)  
        .equ NVMCTRL_PSZ_Shift, 16   @ bitWidth 3 (SmartEEPROM Page Size)  
 
@ -----------------------------------------------------------------------------
  Wortbirne Flag_visible, "8flash!" @ Writes 8 Bytes at once into Flash with ECC.
eightflashstore: @ ( x1 x2 addr -- ) x1 contains LSB of those 64 bits.
@ -----------------------------------------------------------------------------
  @ Check if this goes into core - don't allow that ! No need to check for because of the second check.
  @ Perform write only if desired destination is in erased state...

  push {r0, r1, r2, r3, lr} @ Saving registers is necessary for "flash8bytesblockwrite" emulation layer !

  movs r0, #7
  ands r0, tos
  beq 1f
    Fehler_Quit "8flash! needs 8-aligned address !"
1:

  @ Ist die gewünschte Stelle im Flash-Dictionary ? Außerhalb des Forth-Kerns ?

  ldr r3, =Kernschutzadresse
  cmp tos, r3
  blo.n flash_error

  ldr r3, =FlashDictionaryEnde
  cmp tos, r3
  bhs.n flash_error

  @ Ist die gewünschte Stelle noch unbeschrieben ?

  ldr r0, [tos]
  adds r0, #1
  bne.n flash_twice

  ldr r0, [tos, #4]
  adds r0, #1
  bne.n flash_twice

  @ Daten holen, die geschrieben werden sollen.

  ldmia psp!, {r1}
  ldmia psp!, {r0}

  @ Prüfe Inhalt. Schreibe nur, wenn es NICHT -1 -1 ist.

  movs r3, r0
  ands r3, r1
  cmp r3, #-1
  beq 2f @ Fertig ohne zu Schreiben

  @ Okay, alle Proben bestanden. 
  @ WMODE = ADW
  ldr r3, =NVMCTRL_CTRLA
  ldrh r2, [r3]
  bic r2, #(3<<NVMCTRL_WMODE_Shift)
  orr r2, #(1<<NVMCTRL_WMODE_Shift)
  strh r2, [r3]

  str r0, [tos]
  str r1, [tos, #4]

  ldr r3, =NVMCTRL_CTRLB
  ldrh r2, =0xA504 @ WQW command with Execution Key A5
  strh r2, [r3]

  ldr r3, =NVMCTRL_STATUS
1:ldrh r2, [r3]
  ands r2, #(1<<NVMCTRL_READY_Shift)
  beq 1b

2:drop
  pop {r0, r1, r2, r3, pc}

flash_error:
  Fehler_Quit "Wrong address or data for writing flash !"

flash_twice:
  Fehler_Quit "Flash cannot be written twice !"

.global flashblockerase
@ -----------------------------------------------------------------------------
  Wortbirne Flag_visible, "flashblockerase" @ ( Addr -- )
  @ Löscht einen 8kb großen Flashblock  Deletes one 8kb Flash block
flashblockerase:
@ -----------------------------------------------------------------------------
  push {r0, r1, r2, r3, lr}
  popda r0 @ Adresse zum Löschen holen Fetch address to erase.

  @ Ist die gewünschte Stelle im Flash-Dictionary ? Außerhalb des Forth-Kerns ? Don't erase Forth core.
  ldr r3, =Kernschutzadresse
  cmp r0, r3
  blo 2f

  @ Okay, alle Proben bestanden. 

  @ set addr
  ldr r3, =NVMCTRL_ADDR
  str r0, [r3]

  @ ldr r3, =NVMCTRL_CTRLB
  @ ldrh r2, =0xA512 @ UNLOCK command with Execution Key A5
  @ strh r2, [r3]

  ldr r3, =NVMCTRL_STATUS
1:ldrh r2, [r3]
  ands r2, #(1<<NVMCTRL_READY_Shift)
  beq 1b

  @ ldr r3, =NVMCTRL_INTFLAG
  @ ldrh r2, [r3]
  @ strh r2, [r3]

  @ then issue Erase Page CMD
  ldr r3, =NVMCTRL_CTRLB
  movw r2, #0xA501 @ EB command with Execution Key A5
  strh r2, [r3]
  
  ldr r3, =NVMCTRL_STATUS
3:ldrh r2, [r3]
  ands r2, #(1<<NVMCTRL_READY_Shift)
  beq 3b

2:pop {r0, r1, r2, r3, pc}

.global eraseflash_intern
@ -----------------------------------------------------------------------------
  Wortbirne Flag_visible, "eraseflash" @ ( -- )
  @ Löscht den gesamten Inhalt des Flashdictionaries.
@ -----------------------------------------------------------------------------
        ldr r0, =FlashDictionaryAnfang
eraseflash_intern:
        ldr r1, =FlashDictionaryEnde
        ldr r2, =0xFFFF

1:      ldrh r3, [r0]
        cmp r3, r2
        beq 2f
        pushda r0
        dup
        write "Erase block at  "
        bl hexdot
        writeln " from Flash"
        bl flashblockerase
2:      adds r0, #2
        cmp r0, r1
        bne 1b
  writeln "Finished. Reset !"
  bl mecrisp_core

@ -----------------------------------------------------------------------------
  Wortbirne Flag_visible, "eraseflashfrom" @ ( Addr -- )
  @ Beginnt an der angegebenen Adresse mit dem Löschen des Dictionaries.
@ -----------------------------------------------------------------------------
        popda r0
        b.n eraseflash_intern
