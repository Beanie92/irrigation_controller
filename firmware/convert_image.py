from PIL import Image

def to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

try:
    img = Image.open('src/logo_small.jpg')
    width, height = img.size
    img_rgb = img.convert('RGB')

    with open('src/logo.h', 'w') as f:
        f.write('// Auto-generated logo data\n')
        f.write(f'// Image size: {width}x{height} pixels\n')
        f.write('// Format: RGB565\n\n')
        f.write('#ifndef LOGO_H\n')
        f.write('#define LOGO_H\n\n')
        f.write('#include <stdint.h>\n\n')
        f.write(f'#define LOGO_WIDTH {width}\n')
        f.write(f'#define LOGO_HEIGHT {height}\n\n')
        f.write(f'const uint16_t logo_data[{width}*{height}] = {{\n')

        pixels = list(img_rgb.getdata())
        for i, p in enumerate(pixels):
            r, g, b = p
            rgb565 = to_rgb565(r, g, b)
            f.write(f'0x{rgb565:04X}, ')
            if (i + 1) % 12 == 0:
                f.write('\n')
        
        f.write('\n};\n\n')
        f.write('#endif // LOGO_H\n')

    print("logo.h generated successfully.")

except FileNotFoundError:
    print("Error: logo_small.jpg not found. Make sure the image is in the 'src' directory.")
except Exception as e:
    print(f"An error occurred: {e}")
