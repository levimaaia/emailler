; SETMAC.SYSTEM
; Sets the MAC address on an Uthernet-II,
; then launches next .SYSTEM file on drive (if any)
;
; Based on ...
; Fully disassembled and analyzed source to SMT
; NS.CLOCK.SYSTEM by M.G. - 04/20/2017

; other notes:
; * uses direct block access to read volume directory,
;   so won't launch from an AppleShare volume.

; Build instructions:
; ca65 setmac.s -l setmac.lst
; ld65 -t none -o setmac.system setmac.o
; put setmac.system as a SYS file on a ProDOS disk.

        .setcpu "6502"

; ----------------------------------------------------------------------------
; Zero page
POINTER         := $A5                          ; generic pointer used everywhere
ENTNUM          := $A7                          ; current file entry # in block (zero-based)
LENGTH          := $A8                          ; generic length byte used everywhere

; entry points
PRODOS          := $BF00
INIT            := $FB2F
HOME            := $FC58
CROUT           := $FD8E
PRBYTE          := $FDDA
COUT            := $FDED
SETNORM         := $FE84
SETKBD          := $FE89
SETVID          := $FE93

; buffers & other spaces
INBUF           := $0200                        ; input buffer
PATHBUF         := $0280                        ; path buffer
RELOCT          := $1000                        ; relocation target
BLOCKBUF        := $1800                        ; block & I/O buffer
SYSEXEC         := $2000                        ; location of SYS executables
SOFTEV          := $03F2                        ; RESET vector

; global Page entries
CLKENTRY        := $BF06                        ; clock routine entry point
DEVNUM          := $BF30                        ; most recent accessed device
MEMTABL         := $BF58                        ; system bitmap
DATELO          := $BF90
DATEHI          := $BF91
TIMELO          := $BF92
TIMEHI          := $BF93
MACHID          := $BF98                        ; machine ID

; I/O and hardware
ROMIn2          := $C082                        ; access to read ROM/no write LC RAM
LCBank1         := $C08B                        ; Access twice to write LC bank 1
KBD             := $C000                        ; keyboard
INTCXROMOFF     := $C006                        ; Disable internal $C100-$CFFF ROM
INTCXROMON      := $C007                        ; Enable interal $C100-$CFFF ROM
KBDSTR          := $C010                        ; keyboard strobe
INTCXROM        := $C015                        ; Read state of $C100-$CFFF soft switch
CLR80VID        := $C00C                        ; Turn off 80-column mode
CLRALTCHAR      := $C00E                        ; Turn off alt charset
SLOT3ROM        := $C300                        ; SLOT 3 ROM
C8OFF           := $CFFF                        ; C8xx Slot ROM off
IOMINUSONE      := $BFFF                        ; For IOMINUSONE,y addressing

; Misc
CLKCODEMAX      := $7D

; Macro to define ASCII string with high bit set.
.macro  hasc Arg
  .repeat .strlen(Arg), I
    .byte   .strat(Arg, I) | $80
  .endrep
.endmacro

; ----------------------------------------------------------------------------
; relocate ourself from SYSEXEC to RELOCT
; note that we .org the whole thing, including this routine, at the target
; address and jump to it after the first page is relocated.
        .org    RELOCT                          ; note code initially runs at SYSEXEC
.proc   Relocate
        sec
        bcs     :+                              ; skip version info
        .byte   $04, $21, $91                   ; version date in BCD
:       ldx     #$05                            ; page counter, do $0500 bytes
        ldy     #$00                            ; byte counter
from:   lda     SYSEXEC,y                       ; start location
to:     sta     RELOCT,y                        ; end location
        iny                                     ; next byte offset
        bne     from                            ; if not zero, copy byte
        inc     from+2                          ; otherwise increment source address high byte
        inc     to+2                            ; and destination address high byte
        dex                                     ; dec page counter
        beq     Main                            ; when done start main code
        jmp     from                            ; live jump... into relocated code after first page loop
.endproc
; ----------------------------------------------------------------------------
.proc   Main
        ; figure out length of our name and stick in LENGTH
        lda     #$00
        sta     LENGTH                          ; zero length
        ldx     PATHBUF                         ; length pf path
        beq     Main1                           ; skip if length = 0 (no path)
:       inc     LENGTH                          ; length += 1 for each non-/
        dex                                     ; previous char in path
        beq     CopyNm                          ; nothing left?  Copy our name.
        lda     PATHBUF,x                       ; get character
        ; check for /... kinda wtf, as cmp #$2f would work...
        eor     #$2F                            ; roundabout check for '/'
        asl     a                               ; upper/lower case
        bne     :-                              ; keep examining if not '/'
        ; now save our name (assuming we weren't lied to)
CopyNm: ldy     #$00                            ; init destination offset
:       iny                                     ; increment destination offset
        inx                                     ; inc source offset
        lda     PATHBUF,x                       ; get source char
        sta     MyName,y                        ; write to save location
        cpy     LENGTH                          ; got it all?
        bcc     :-                              ; nope, copy more
        sty     MyName                          ; save length
        ; done moving stuff
Main1:  cld
        bit     ROMIn2                          ; make sure ROM enabled
        ; letter of the law with RESET vector
        lda     #<Main1
        sta     SOFTEV
        lda     #>Main1
        sta     SOFTEV+1
        eor     #$A5
        sta     SOFTEV+2
        lda     #$95                            ; control code
        jsr     COUT                            ; to quit 80-column firmware
        ldx     #$FF                            ; reset
        txs                                     ; stack pointer
        ; get video & keyboard I/O to known state
        sta     CLR80VID
        sta     CLRALTCHAR
        jsr     SETVID
        jsr     SETKBD
        jsr     SETNORM
        jsr     INIT
        ; initialize memory map
        ldx     #$17                            ; there are $18 bytes total
        lda     #$01                            ; last byte gets $01 (protect global page)
:       sta     MEMTABL,x
        lda     #$00                            ; all but first byte get $00 (no protection)
        dex
        bne     :-
        lda     #$CF                            ; first byte protect ZP, stack, text page 1
        sta     MEMTABL
        lda     MACHID
        and     #$88                            ; mask in bits indicating machine with lower case
        bne     :+                              ; has lower case, skip over next few instructions
        lda     #$DF                            ; mask value
        sta     CaseCv                          ; to make print routine convert to upper case  
:       jsr     HOME
        jsr     iprint
        .byte   $8D                             ; CR
        hasc    "Uthernet-II SETMAC Utility"
        .byte   $8D,$00                         ; CR, done
        lda     #5                              ; Slot 5 TODO: This is hardcoded for now
        jsr     setmac
        jmp     NextSys
.endproc
.proc   setmac
; Set the MAC address on the Uthernet-II
; Expects slot number in A
        asl
        asl
        asl
        asl
        clc
        adc     #$85
        tay                                  ; Mode register offset
        lda     #$80                         ; Reset W5100
        sta     IOMINUSONE,y                 ; Store in MODE register
        lda     #$03                         ; Address autoinc, indirect
        sta     IOMINUSONE,y                 ; Store in MODE register
        iny                                  ; $d6
        lda     #$00                         ; High byte of MAC reg addr
        sta     IOMINUSONE,y                 ; Set high byte of pointer
        iny                                  ; $d7
        lda     #$09                         ; Low byte
        sta     IOMINUSONE,y                 ; Set low byte
        ldx     #$00
        iny                                  ; $d8
:       lda     mac,x                        ; Load byte of MAC
        sta     IOMINUSONE,y                 ; Set and autoinc
        inx
        cpx     #6
        bne     :-
        dey
        dey                                  ; $d6
        lda     #$00                         ; High byte of $001a reg addr
        sta     IOMINUSONE,y                 ; Set high byte of pointer
        iny                                  ; $d7
        lda     #$1a                         ; Low byte
        sta     IOMINUSONE,y                 ; Set low byte
        lda     #$06                         ; Magic value: MAC set!
        iny                                  ; $d8
        sta     IOMINUSONE,y                 ; Set and autoinc
        rts
mac:    .byte   $00,$08,$0d,$00,$de,$ad      ; TODO: Hardcoded for now
.endproc
; This starts the process of finding & launching next system file
; unfortunately it also uses block reads and can't be run from an AppleShare
; volume. 
.proc   NextSys
        ; set reset vector to DoQUit
        lda     #<DoQuit
        sta     SOFTEV
        lda     #>DoQuit
        sta     SOFTEV+1
        eor     #$A5
        sta     SOFTEV+2
        lda     DEVNUM                          ; last unit number accessed
        sta     PL_READ_BLOCK+1                 ; put in parameter list
        jsr     GetBlock                        ; get first volume directory block
        lda     BLOCKBUF+$23                    ; get entry_length
        sta     SMENTL                          ; modify code
        lda     BLOCKBUF+$24                    ; get entries_per_block
        sta     SMEPB                           ; modify code
        lda     #$01                            ; 
        sta     ENTNUM                          ; init current entry number as second (from 0)
        lda     #<(BLOCKBUF+$2B)                ; set pointer to that entry
        sta     POINTER                         ; making assumptions as we go
        lda     #>(BLOCKBUF+$2B)
        sta     POINTER+1
        ; loop to examine file entries...
FEntLp: ldy     #$10                            ; offset of file_type
        lda     (POINTER),y
        cmp     #$FF                            ; SYS?
        bne     NxtEnt                          ; nope..
        ldy     #$00                            ; offset of storage_type & name_length
        lda     (POINTER),y
        and     #$30                            ; mask interesting bits of storage_type
        beq     NxtEnt                          ; if not 1-3 (standard file organizations)
        lda     (POINTER),y                     ; get storage_type and name_length again
        and     #$0F                            ; mask in name_length
        sta     LENGTH                          ; save for later
        tay                                     ; and use as index for comparison
        ; comparison loop
        ldx     #$06                            ; counter for size of ".SYSTEM"
:       lda     (POINTER),y                     ; get file name byte
        cmp     system,x                        ; compare to ".SYSTEM"
        bne     NxtEnt                          ; no match
        dey
        dex
        bpl     :-
        ; if we got here, have ".SYSTEM" file
        ldy     MyName                          ; length of our own file name
        cpy     LENGTH                          ; matches?
        bne     CkExec                          ; nope, see if we should exec
        ; loop to check if this is our own name
:       lda     (POINTER),y
        cmp     MyName,y
        bne     CkExec                          ; no match, see if we should exec
        dey
        bne     :-
        ; if we got here, we have found our own self
        sec
        ror     FdSelf                          ; flag it
        ; go to next entry
NxtEnt: lda     POINTER                         ; low byte of entry pointer
        clc                                     ; ready for addition
        adc     #$27                            ; add entry length that is
SMENTL  = * - 1                                 ; self-modifed
        sta     POINTER                         ; save it
        bcc     :+                              ; no need to do high byte if no carry
        inc     POINTER+1                       ; only increment if carry
:       inc     ENTNUM                          ; next entry number
        lda     ENTNUM                          ; and get it
        cmp     #$0D                            ; check against number of entries that is
SMEPB   = * - 1                                 ; self-modified
        bcc     FEntLp                          ; back to main search if not done with this block
        lda     BLOCKBUF+$02                    ; update PL_BLOCK_READ for next directory block
        sta     PL_READ_BLOCK+4
        lda     BLOCKBUF+$03
        sta     PL_READ_BLOCK+5
        ora     PL_READ_BLOCK+4                 ; see if pointer is $00
        beq     NoSys                           ; error if we hit the end and found nothing..
        jsr     GetBlock                        ; get next volume directory block
        lda     #$00
        sta     ENTNUM                          ; update current entry number
        lda     #<(BLOCKBUF+$04)                ; and reset pointer
        sta     POINTER
        lda     #>(BLOCKBUF+$04)
        sta     POINTER+1
        jmp     FEntLp                          ; go back to main loop
CkExec: bit     FdSelf                          ; did we find our own name yet?
        bpl     NxtEnt                          ; nope... go to next entry
        ldx     PATHBUF                         ; get length of path in path buffer
        beq     CpyNam                          ; skip looking for / if zero
:       dex                                     ; 
        beq     CpyNam                          ; done if zero
        lda     PATHBUF,x                       ; 
        eor     #$2F                            ; is '/'?
        asl     a                               ; in roundabout way
        bne     :-                              ; no slash
        ; copy file name onto path, x already has position
CpyNam: ldy     #$00
:       iny                                     ; next source byte offset
        inx                                     ; next dest byte offset
        lda     (POINTER),y                     ; get filename char
        sta     PATHBUF,x                       ; put in path
        cpy     LENGTH                          ; copied all the chars?
        bcc     :-                              ; nope
        stx     PATHBUF                         ; update length of path
        jmp     LaunchSys                       ; try to launch it!
NoSys:  jsr     iprint
        .byte   $8D, $8D, $8D
        hasc    "* Unable to find next '.SYSTEM' file *"
        .byte   $8D, $00
        ; wait for keyboard then quit to ProDOS
        bit     KBDSTR
:       lda     KBD
        bpl     :-   
        bit     KBDSTR
        jmp     DoQuit
.endproc
; ----------------------------------------------------------------------------
; inline print routine
; print chars after JSR until $00 encountered
; converts case via CaseCv ($FF = no conversion, $DF = to upper)
.proc   iprint
        pla
        sta     POINTER
        pla
        sta     POINTER+1
        bne     next
:       cmp     #$E1
        bcc     noconv
        and     CaseCv
noconv: jsr     COUT
next:   inc     POINTER
        bne     nohi
        inc     POINTER+1
nohi:   ldy     #$00
        lda     (POINTER),y
        bne     :-
        lda     POINTER+1
        pha
        lda     POINTER
        pha
        rts
.endproc
; ----------------------------------------------------------------------------
; print one or two decimal digits
.proc   PrDec
        ldx     #$B0                            ; tens digit
        cmp     #$0A
        bcc     onedig
:       sbc     #$0A                            ; repeated subtraction, carry is already set
        inx
        cmp     #$0A                            ; less than 10 yet?
        bcs     :-                              ; nope
onedig: pha
        cpx     #$B0
        beq     nozero                          ; skip printing leading zero
        txa
        jsr     COUT
nozero: pla
        ora     #$B0
        jsr     COUT
        rts
.endproc
CaseCv: .byte   $FF                            ; default case conversion byte = none
; ----------------------------------------------------------------------------
.proc   DoQuit
        jsr     PRODOS
        .byte   $65                             ; MLI QUIT
        .word   PL_QUIT
        brk                                     ; crash into monitor if QUIT fails
        rts                                     ; (!) if that doesn't work, go back to caller
PL_QUIT:
        .byte   $04                             ; param count
        .byte   $00                             ; quit type - $00 is only type
        .word   $0000                           ; reserved
        .byte   $00                             ; reserved
        .word   $0000                           ; reserved
.endproc
.proc   GetBlock
        jsr     PRODOS
        .byte   $80                             ; READ_BLOCK
        .word   PL_READ_BLOCK
        bcs     LaunchFail
        rts
.endproc
PL_READ_BLOCK:
        .byte   $03
        .byte   $60                             ; unit number
        .word   BLOCKBUF
        .word   $0002                           ; first volume directory block
; ----------------------------------------------------------------------------
; launch next .SYSTEM file
.proc   LaunchSys
        jsr     PRODOS
        .byte   $C8                             ; OPEN
        .word   PL_OPEN
        bcs     LaunchFail
        lda     PL_OPEN+$05                     ; copy ref number
        sta     PL_READ+$01                     ; into READ parameter list
        jsr     PRODOS
        .byte   $CA                             ; READ
        .word   PL_READ
        bcs     LaunchFail
        ; bug the first:  Close should be done every time the OPEN call is successful
        ; but only done when the READ succeeds
        ; bug the second:  Others may not consider this a bug, but we close all open
        ; files, even if we didn't open them.  That's not very polite.
        jsr     PRODOS
        .byte   $CC                             ; CLOSE
        .word   PL_CLOSE
        bcs     LaunchFail
        jmp     SYSEXEC
.endproc
; ----------------------------------------------------------------------------
; failed to launch next .SYSTEM file
.proc   LaunchFail
        pha
        jsr     iprint
        .byte   $8D, $8D, $8D
        hasc    "**  Disk Error $"
        .byte   $00
        pla
        jsr     PRBYTE
        jsr     iprint
        hasc    "  **"
        .byte   $8D, $00
        ; wait for keyboard, then quit to ProDOS
        bit     KBDSTR
:       lda     KBD
        bpl     :-
        bit     KBDSTR
        jmp     DoQuit
.endproc
; ----------------------------------------------------------------------------
PL_OPEN:
        .byte   $03
        .word   PATHBUF
        .word   BLOCKBUF
        .byte   $01                             ; ref num (default 1 wtf?)
PL_READ:
        .byte   $04
        .byte   $01                             ; ref num
        .word   SYSEXEC                         ; data buffer
        .word   $FFFF                           ; request count
        .word   $0000                           ; transfer count
PL_CLOSE:
        .byte   $01
        .byte   $00                             ; ref num $00 = all files
; ----------------------------------------------------------------------------
FdSelf: .byte   $00                             ; bit 7 set if we found our name in volume dir
system: .byte   ".SYSTEM"
MyName: .byte   $0F,"SETMAC.SYSTEM"

