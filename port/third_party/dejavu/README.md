DejaVu Sans Mono 2.37 (Debian fonts-dejavu-core), cut down to printable ASCII without hinting (stb_truetype
ignores it). Bitstream Vera licence with the DejaVu changes in the public domain: LICENSE. The developer console's
font (code/console/devconsole.cpp). Remade with:

    pyftsubset DejaVuSansMono.ttf --unicodes=U+0020-007E --no-hinting --desubroutinize --layout-features='' \
        --name-IDs='*' --output-file=DejaVuSansMono-ascii.ttf
    { echo "// Generated from DejaVuSansMono-ascii.ttf (README.md). Bitstream Vera licence: LICENSE."
      echo "static const unsigned char kDejaVuSansMonoAscii[] = {"; xxd -i < DejaVuSansMono-ascii.ttf; echo "};"
    } > dejavusansmono_ascii.h
