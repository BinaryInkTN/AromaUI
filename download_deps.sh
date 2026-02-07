#!/bin/bash
set -e

# Download FreeType
echo "Downloading FreeType..."
wget https://download.savannah.gnu.org/releases/freetype/freetype-2.13.2.tar.gz
tar -xf freetype-2.13.2.tar.gz -C vendors/
mv vendors/freetype-2.13.2 vendors/freetype
rm freetype-2.13.2.tar.gz

echo "FreeType downloaded to vendors/freetype"
