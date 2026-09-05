; Test program for the 8086 interpreter in main.c ('x86' command).
; Assemble with: nasm -f bin hello8086.asm -o hello8086.bin
; Then `xxd -i hello8086.bin` to regenerate the x86_hello_program[] byte
; array in main.c if this source ever changes.
;
; Deliberately uses only the instructions the interpreter implements: MOV
; (reg16/reg8, imm and reg-to-reg), LODSB, CMP, JE, JMP, and INT 0x21 as a
; tiny DOS-style syscall (AH=02h print char in DL, AH=4Ch halt with code
; in AL) -- not real DOS, just a familiar/idiomatic convention.
BITS 16
ORG 0

start:
    mov si, msg
.next_char:
    lodsb
    cmp al, 0
    je .done
    mov dl, al
    mov ah, 0x02
    int 0x21
    jmp .next_char
.done:
    mov ah, 0x4c
    int 0x21

msg: db 'Hello from 8086 land!', 13, 10, 0
