;NB65 API example in DASM format (http://www.atari2600.org/DASM/)
  processor 6502

  include "../inc/nb65_constants.i"

  ;useful macros 
  mac     ldax
    lda     [{1}]
    ldx     [{1}]+1
  endm

  mac     ldaxi
    lda     #<[{1}]
    ldx     #>[{1}]
  endm

  mac     stax
    sta     [{1}]
    stx     [{1}]+1
  endm

  mac cout
    lda [{1}]
    jsr print_a
  endm   

  mac print_cr
    cout #13
    jsr print_a
  endm

  mac nb65call
    ldy [{1}]
    jsr NB65_DISPATCH_VECTOR   
  endm

  mac print
    
    ldaxi [{1}]
    ldy #NB65_PRINT_ASCIIZ
    jsr NB65_DISPATCH_VECTOR   
  endm


;some routines & zero page variables
print_a   equ $ffd2
temp_ptr  equ $FB ; scratch space in page zero


;start of code
;NO BASIC stub! needs to be direct booted via TFTP
  org $1000
    
  ldaxi #NB65_CART_SIGNATURE  ;where signature should be in cartridge (if cart is banked in)
  jsr  look_for_signature
  bcc found_nb65_signature

  ldaxi #NB65_RAM_STUB_SIGNATURE  ;where signature should be in a RAM stub
  jsr  look_for_signature
  bcs nb65_signature_not_found
  jsr NB65_RAM_STUB_ACTIVATE     ;we need to turn on NB65 cartridge
  jmp found_nb65_signature
  
nb65_signature_not_found
  ldaxi #nb65_api_not_found_message
  jsr print_ax
  rts

found_nb65_signature

  lda NB65_API_VERSION
  cmp #02
  bpl .version_ok
  print incorrect_version
  jmp reset_after_keypress    
.version_ok  
  print #initializing
  nb65call #NB65_INITIALIZE
	bcc .init_ok
  print_cr
  print #failed
  print_cr
  jsr print_errorcode
  jmp reset_after_keypress    
.init_ok

;if we got here, we have found the NB65 API and initialised the IP stack
;print out the current configuration
  nb65call #NB65_PRINT_IP_CONFIG


listen_on_port_80

 ldaxi #scratch_buffer
 stax tcp_buffer_ptr
 print #waiting
 ldaxi #80 ;port number
 stax nb65_param_buffer+NB65_TCP_PORT
 stx nb65_param_buffer+NB65_TCP_REMOTE_IP
 stx nb65_param_buffer+NB65_TCP_REMOTE_IP+1
 stx nb65_param_buffer+NB65_TCP_REMOTE_IP+2
 stx nb65_param_buffer+NB65_TCP_REMOTE_IP+3
 ldaxi #http_callback
 stax nb65_param_buffer+NB65_TCP_CALLBACK
 ldaxi #nb65_param_buffer 
 nb65call #NB65_TCP_CONNECT ;wait for inbound connect
 bcc  .connected_ok
 print  #error_while_waiting
 jsr  print_errorcode
 jmp reset_after_keypress    
.connected_ok
  print #ok
  lda #0
  sta connection_closed
  sta found_eol
  
.main_polling_loop
  jsr NB65_PERIODIC_PROCESSING_VECTOR
  lda found_eol
  beq .no_eol_yet
  nb65call #NB65_PRINT_HEX
  lda #"!"
  jsr print_a
  ldaxi #html_length
  stax nb65_param_buffer+NB65_TCP_PAYLOAD_LENGTH
  ldaxi #html
  stax nb65_param_buffer+NB65_TCP_PAYLOAD_POINTER
  ldaxi  #nb65_param_buffer
  nb65call  #NB65_SEND_TCP_PACKET

  nb65call #NB65_TCP_CLOSE_CONNECTION  
  jmp listen_on_port_80
.no_eol_yet  
  lda connection_closed
  beq .main_polling_loop  
  jmp listen_on_port_80




;look for NB65 signature at location pointed at by AX
look_for_signature subroutine
  stax temp_ptr
  ldy #3
.check_one_byte
  lda (temp_ptr),y
  cmp nb65_signature,y
  bne .bad_match  
  dey 
  bpl .check_one_byte  
  clc
  rts
.bad_match
  sec
  rts

print_ax subroutine
  stax temp_ptr
  ldy #0
.next_char 
  lda (temp_ptr),y
  beq .done
  jsr print_a
  iny
  jmp .next_char
.done
  rts
  
get_key
  jsr $ffe4
  cmp #0
  beq get_key
  rts

reset_after_keypress
  print  #press_a_key_to_continue    
  jsr get_key
  jmp $fce2   ;do a cold start


print_errorcode
  print #error_code
  nb65call #NB65_GET_LAST_ERROR
  nb65call #NB65_PRINT_HEX
  print_cr
  rts

;http callback - will be executed whenever data arrives on the TCP connection
http_callback  
  
  ldaxi #nb65_param_buffer
  nb65call #NB65_GET_INPUT_PACKET_INFO  
  lda nb65_param_buffer+NB65_PAYLOAD_LENGTH+1
  cmp #$ff
  bne .not_eof
  lda #1
  sta connection_closed
  rts
.not_eof  

  lda #"*"
  jsr print_a

  ldax nb65_param_buffer+NB65_PAYLOAD_POINTER
  stax tcp_inbound_data_ptr
  ldax nb65_param_buffer+NB65_PAYLOAD_LENGTH 
  stax tcp_inbound_data_length
  
;copy this chunk to our input buffer
  ldax tcp_buffer_ptr  
  stax nb65_param_buffer+NB65_BLOCK_DEST
  ldax tcp_inbound_data_ptr
  stax nb65_param_buffer+NB65_BLOCK_SRC
  ldax tcp_inbound_data_length
  stax nb65_param_buffer+NB65_BLOCK_SIZE
  ldaxi #nb65_param_buffer
  nb65call #NB65_BLOCK_COPY


;increment the pointer into the input buffer  
  clc
  lda tcp_buffer_ptr
  adc tcp_inbound_data_length
  sta tcp_buffer_ptr
  sta temp_ptr
  lda tcp_buffer_ptr+1
  adc tcp_inbound_data_length+1
  sta tcp_buffer_ptr+1  
  sta temp_ptr
  
;put a null byte at the end (assumes we have set temp_ptr already)
  lda #0
  tay
  sta (temp_ptr),y
    
;look for EOL ($0D) in input
  sta found_eol
  ldaxi #scratch_buffer
  stax get_next_byte+1

.look_for_eol
  jsr get_next_byte
  cmp #$0a
  bne .found_eol    
  cmp #$0d
  bne .not_eol
.found_eol  
  inc found_eol
  rts
.not_eol  
  cmp #0
  bne .look_for_eol 
  rts


;constants
nb65_api_not_found_message dc.b "ERROR - NB65 API NOT FOUND.",13,0
incorrect_version dc.b "ERROR - NB65 API MUST BE AT LEAST VERSION 2.",13,0

nb65_signature dc.b $4E,$42,$36,$35  ; "NB65"  - API signature
initializing dc.b "INITIALIZING ",13,0
error_code dc.b "ERROR CODE: $",0
error_while_waiting dc.b "ERROR WHILE "
waiting dc.b "WAITING FOR CLIENT CONNECTION",13,0
press_a_key_to_continue    dc.b "PRESS ANY KEY TO CONTINUE",0
mode dc.b " MODE",13,0
disconnected dc.b 13,"CONNECTION CLOSED",13,0
failed dc.b "FAILED ", 0
ok dc.b "OK ", 0
transmission_error dc.b "ERROR WHILE SENDING ",0

html dc.b "<H1>c64 test page</h1><form method=post>foo:<input type=text name=foo><br>bar:<textarea name=bar></textarea><br><INPUT type=submit value=send>"
html_end
html_length equ html_end-html

;self modifying code
get_next_byte
  lda $ffff
  inc get_next_byte+1
  bne .skip
  inc get_next_byte+2
.skip
  rts


;variables
connection_closed ds.b 1
found_eol ds.b 1
nb65_param_buffer DS.B $20  
tcp_buffer_ptr  ds.b 2
scan_ptr  ds.b 2
tcp_inbound_data_ptr ds.b 2
tcp_inbound_data_length ds.b 2
scratch_buffer: DS.B $1000

