import base64

try:
    with open('src/logo.webp', 'rb') as image_file:
        encoded_string = base64.b64encode(image_file.read()).decode('utf-8')
        print("Copy the following line into your C++ code:")
        print(f'const char* favicon_base64 = "data:image/webp;base64,{encoded_string}";')
except FileNotFoundError:
    print("Error: src/logo_small.jpg not found.")
except Exception as e:
    print(f"An error occurred: {e}")
