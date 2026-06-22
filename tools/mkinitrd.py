import sys

with open(sys.argv[1], 'rb') as f:
    data = f.read()

print(f'// Auto-generated from {sys.argv[1]}')
print(f'#define INITRD_INIT_SIZE {len(data)}')
print('static const uint8_t initrd_init_data[INITRD_INIT_SIZE] __attribute__((used)) = {')
for i in range(0, len(data), 16):
    line = ', '.join(f'0x{b:02X}' for b in data[i:i+16])
    print(f'    {line},')
print('};')
