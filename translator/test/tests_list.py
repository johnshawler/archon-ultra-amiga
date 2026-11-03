from xl import CF, ZF, SF, OF, ALL
R32 = ['eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp']
T = []
for op in ('add', 'sub', 'and', 'or', 'xor', 'cmp', 'adc', 'sbb'):
    T += [f'{op} eax, ebx', f'{op} esi, 0x12345', f'{op} ecx, -3', f'{op} ax, bx', f'{op} al, bl', f'{op} dl, 0x7f',
          f'{op} edi, dword ptr [ebx+8]', f'{op} dword ptr [ebx+8], eax', f'{op} dword ptr [esi+edi*4+16], 0x1234', f'{op} byte ptr [ecx-4], dl',
          f'{op} word ptr [ebp+6], ax', f'{op} al, byte ptr [esi]']
T += ['test eax, eax', 'test eax, ebx', 'test al, 0x80', 'test eax, 0x100', 'test al, al', 'test dword ptr [esi], 0xff00']
T += ['mov eax, ebx', 'mov eax, 0x12345678', 'mov ax, 0x1234', 'mov al, 0x12', 'mov eax, dword ptr [ebx+4]', 'mov dword ptr [ebx+4], eax',
      'mov dword ptr [ebx+esi*4], 0x11223344', 'mov word ptr [ecx], dx', 'mov byte ptr [ecx-2], bl', 'mov ax, word ptr [edx+2]', 'mov bl, byte ptr [esi]',
      'mov ah, 0x12', 'mov al, ah', 'mov ah, bl', 'mov ah, al', 'mov ch, cl', 'mov dh, ch', 'mov ch, ah', 'mov dword ptr [0x14080], edx', 'mov eax, dword ptr [esp+4]', 'mov dword ptr [ebp-8], ecx']
T += ['movzx eax, bl', 'movzx eax, byte ptr [ebx]', 'movzx ecx, word ptr [esi]', 'movzx ax, bl', 'movsx eax, bl', 'movsx edx, word ptr [esi+2]', 'movsx eax, byte ptr [edi]', 'movsx ecx, cx', 'movsx eax, bh', 'movzx edx, ch']
T += ['xchg ah, cl', 'xchg cl, bh', 'xchg dl, ah']
T += ['xor ah, ah', 'sub ch, ch', 'add ah, al', 'add ah, ch', 'sub dh, dl', 'or ah, 0x12', 'and bh, bl', 'cmp ah, al', 'xor bh, bh', 'add al, ah']
T += ['cmp cx, 0x834', 'cmp ax, 0x64', 'xchg cx, ax', 'neg ax', 'sub ax, cx', 'not bl', 'and bl, bh', 'xchg ah, al', 'cmp bx, 0x8000', 'cmp ax, -1', 'sub cx, 0x834']
T += ['movzx edx, dx', 'movzx eax, al', 'movzx ecx, cl', 'movsx edx, dx', 'movsx eax, al', 'movsx esi, si']
T += ['lea eax, [ebx+esi*4+8]', 'lea ecx, [edx+7]', 'lea edi, [esi*8+0x1000]', 'lea eax, [ebp-12]', 'lea eax, [esp+8]', 'lea eax, [ebx+ecx]']
T += ['xchg eax, ebx', 'xchg ax, cx', 'xchg al, dl', 'xchg eax, dword ptr [esi]']
T += ['cwde', 'cbw', 'cdq']
for op in ('inc', 'dec'):
    T += [(f'{op} eax', ZF | SF | OF | CF), (f'{op} dx', ZF | SF | OF | CF), (f'{op} bl', ZF | SF | OF | CF), (f'{op} dword ptr [esi]', ZF | SF | OF | CF), (f'{op} byte ptr [edi]', ZF | SF | OF | CF)]
T += ['neg eax', 'neg bx', 'neg cl', 'neg dword ptr [esi]', 'not eax', 'not dx', 'not dword ptr [ebx]', 'not byte ptr [ecx]']
for op in ('shl', 'shr', 'sar', 'rol', 'ror'):
    for n in (1, 2, 5, 9, 17):
        T += [(f'{op} eax, {n}', CF | ZF | SF if op in ('shl', 'shr', 'sar') else CF), (f'{op} bx, {n}', CF | ZF | SF if op in ('shl', 'shr', 'sar') else CF)]
    T += [(f'{op} dl, 3', CF | ZF | SF if op in ('shl', 'shr', 'sar') else CF)]
T += [('rcl eax, 1', CF), ('rcr eax, 1', CF), ('rcl ebx, 3', CF)]
T += [('imul eax, ebx', CF | OF), ('imul ecx, dword ptr [esi]', CF | OF), ('imul edx, ebx, 12', CF | OF), ('imul edx, dword ptr [esi], 1000', CF | OF), ('imul ax, bx', CF | OF), ('imul ebx', CF | OF),
      ('mul ebx', CF | OF), ('mul dword ptr [esi]', CF | OF)]
T += [('div ebx', 0), ('idiv ecx', 0)]
T += ['push ax', 'push bx', 'push word ptr [esi]', 'push 0x1234', 'pop bx', 'pop word ptr [esi]', 'shl ch, cl', 'shr bh, cl', 'sar ch, cl']
T += ['push eax', 'push 0x12345678', 'push dword ptr [esi]', 'pop ebx', 'pop dword ptr [esi]', 'leave', 'pushal', 'popal']
T += ['setne al', 'sete bl', 'setg cl', 'sete byte ptr [esi]']
for op in ('adc', 'sbb'):
    T += [f'{op} al, ah', f'{op} ah, bl', f'{op} ch, dh', f'{op} ah, 5', f'{op} dl, ch', f'{op} ah, byte ptr [esi]', f'{op} bh, bh']
T += [('imul bh', CF | OF), ('imul bl', CF | OF), ('imul cx', CF | OF), ('imul word ptr [esi]', CF | OF), ('imul byte ptr [esi]', CF | OF),
      ('mul bl', CF | OF), ('mul bh', CF | OF), ('mul cx', CF | OF)]
T += [('div bl', 0), ('div cx', 0), ('idiv bl', 0), ('idiv cx', 0)]
TESTS = T
