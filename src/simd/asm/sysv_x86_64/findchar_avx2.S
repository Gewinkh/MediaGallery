//  mg_finde_erstes_avx2_sysv - erste Stelle, an der eines von zwei Zeichen steht.
//  Handgeschrieben fuer GENAU EINE Kombination: System-V-Aufrufkonvention auf
//  x86-64, AVX2 vorausgesetzt. Windows uebergibt in anderen Registern und
//  verlangt xmm6-xmm15 vom Gerufenen zurueck; dort und ueberall sonst greift
//  die C++-Fassung. Die Weiche steht in `src/simd/FindChar.h`.
//
//  System V: rdi = p, rsi = n, edx = a, ecx = b, Rueckgabe in rax.
//  Alle benutzten Register (rax, rdx, rcx, r8-r11, ymm0-ymm4) sind fluechtig -
//  nichts zu retten, kein Stapelrahmen.
//
//  Verfahren wie die Intrinsics-Fassung: 16 Zeichen je Durchlauf vergleichen,
//  Treffer als Bitmaske, niedrigstes gesetztes Bit zaehlen. `vpmovmskb` liefert
//  ZWEI Bits je 16-Bit-Zeichen, deshalb die Halbierung.
//
//  FALLE, die beim ersten Schreiben zugeschlagen hat: `vpmovmskb` ueberschreibt
//  sein Zielregister. Lag dort noch eines der gesuchten Zeichen, verglich die
//  Restschleife danach gegen die Maske statt gegen das Zeichen. Beide Zeichen
//  stehen deshalb ab dem ersten Befehl in r9d/r10d, die Maske in r11d.

        .text
        .intel_syntax noprefix
        .globl  mg_finde_erstes_avx2_sysv
        .type   mg_finde_erstes_avx2_sysv, @function
        .p2align 4

mg_finde_erstes_avx2_sysv:
        mov     r9d, edx                // a, fuer die ganze Laufzeit
        mov     r10d, ecx               // b
        xor     eax, eax                // i = 0
        cmp     rsi, 16
        jl      .Lrestschleife          // kuerzer als ein Block: gar nicht erst
                                        // die Register fuellen (s. FindChar_x86.cpp)

        //  Die beiden gesuchten Zeichen auf alle 16 Spuren verteilen.
        vmovd   xmm0, r9d
        vpbroadcastw ymm0, xmm0
        vmovd   xmm1, r10d
        vpbroadcastw ymm1, xmm1

        mov     r8, rsi
        sub     r8, 16                  // letzter Startplatz eines vollen Blocks

.Lblock:
        cmp     rax, r8
        jg      .Lrest
        vmovdqu ymm2, YMMWORD PTR [rdi + rax*2]
        vpcmpeqw ymm3, ymm2, ymm0
        vpcmpeqw ymm4, ymm2, ymm1
        vpor    ymm3, ymm3, ymm4
        vpmovmskb r11d, ymm3            // 2 Bit je Zeichen
        test    r11d, r11d
        jnz     .Ltreffer
        add     rax, 16
        jmp     .Lblock

.Ltreffer:
        //  `bsf` statt `tzcnt`: dasselbe Ergebnis bei gesetztem Bit, aber ohne
        //  BMI1 vorauszusetzen - gefordert ist genau AVX2, nicht mehr.
        bsf     r11d, r11d
        shr     r11d, 1                 // zwei Bit je Zeichen -> Zeichenindex
        movsxd  r11, r11d
        add     rax, r11
        vzeroupper                      // sonst kostet der naechste SSE-Befehl
        ret

        //  Rest hinter dem letzten vollen Block, Zeichen fuer Zeichen. Ihn zu
        //  vektorisieren hiesse, ueber das Ende der Zeichenkette zu lesen.
.Lrest:
        vzeroupper
.Lrestschleife:
        cmp     rax, rsi
        jge     .Lfertig
        movzx   edx, WORD PTR [rdi + rax*2]
        cmp     edx, r9d
        je      .Lfertig
        cmp     edx, r10d
        je      .Lfertig
        inc     rax
        jmp     .Lrestschleife

.Lfertig:
        ret

        .size   mg_finde_erstes_avx2_sysv, .-mg_finde_erstes_avx2_sysv
        //  Ohne diese Zeile nimmt der Binder einen ausfuehrbaren Stapel an.
        .section .note.GNU-stack,"",@progbits
