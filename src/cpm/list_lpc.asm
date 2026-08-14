; Guest LIST / LISTST for 88-LPC (ports 02h / 03h)
; Installed by AltairBios::installListStubs() at 0xF040 after CP/M boots.
; Matches deramp Altair CP/M LPT LISTST/LIST; host captures OUT 03 → /LPn.TXT.
;
; LISTST — return A=00h not ready, A=FFh ready (BDOS convention)
listst:
        in      a,(02h)         ; LPC status
        and     02h             ; bit1 = ready (FIFO has room)
        ret     z               ; A=0 if busy
        ld      a,0ffh
        ret
;
; LIST — character to print in C
list:
        call    listst
        jp      z,list          ; wait until ready
        ld      a,c
        and     7fh             ; 7-bit data
        out     (03h),a         ; LPC data → lp_capture FIFO
        ret
