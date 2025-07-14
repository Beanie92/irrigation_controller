from PIL import Image
import os

# Define paths
source_image_path = 'web-ui/src/assets/logo.webp'
output_dir = 'data'
output_filename = 'favicon.ico'
output_path = os.path.join(output_dir, output_filename)

# Define the desired size for the favicon
favicon_size = (32, 32)

try:
    # Open the source image
    img = Image.open(source_image_path)

    # Resize the image
    img_resized = img.resize(favicon_size, Image.Resampling.LANCZOS)

    # Create the output directory if it doesn't exist
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # Save as an ICO file
    img_resized.save(output_path, format='ICO', sizes=[favicon_size])

    print(f"'{output_path}' created successfully.")

except FileNotFoundError:
    print(f"Error: Source image not found at '{source_image_path}'")
except Exception as e:
    print(f"An error occurred: {e}")
