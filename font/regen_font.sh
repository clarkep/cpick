pyftsubset NotoSansMono-Regular.ttf --output-file=noto_sans_mono_mini.ttf --text-file=used_chars.txt
xxd -i noto_sans_mono_mini.ttf > noto_sans_mono_mini.h
