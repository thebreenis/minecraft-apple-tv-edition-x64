; Preserve the volatile registers the original small native predicate leaves intact.
EXTERN StoreOwnershipPresentation:PROC
PUBLIC StoreOwnershipThunk
.code
StoreOwnershipThunk PROC
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp, 88h
    movdqu [rsp+20h], xmm0
    movdqu [rsp+30h], xmm1
    movdqu [rsp+40h], xmm2
    movdqu [rsp+50h], xmm3
    movdqu [rsp+60h], xmm4
    movdqu [rsp+70h], xmm5
    mov rdx, [rsp+0B8h]
    call StoreOwnershipPresentation
    movdqu xmm0, [rsp+20h]
    movdqu xmm1, [rsp+30h]
    movdqu xmm2, [rsp+40h]
    movdqu xmm3, [rsp+50h]
    movdqu xmm4, [rsp+60h]
    movdqu xmm5, [rsp+70h]
    add rsp, 88h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    ret
StoreOwnershipThunk ENDP
END
