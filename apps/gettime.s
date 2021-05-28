; Call ProDOS MLI GET_TIME call
; Bobbi 2021

.export _gettime

_gettime:
	jsr $bf00			; MLI
	.byte $82			; GET_TIME
	rts
