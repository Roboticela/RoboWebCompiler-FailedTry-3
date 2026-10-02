import io
import urllib.request
import zipfile

URL = "https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip"
WANTED = (
    ("extras/ttf/Inter-Regular.ttf", "kInterRegular"),
    ("extras/ttf/Inter-Bold.ttf", "kInterBold"),
    ("extras/ttf/Inter-Italic.ttf", "kInterItalic"),
    ("extras/ttf/Inter-BoldItalic.ttf", "kInterBoldItalic"),
)
OUTPUT = r"D:\Muhammad\RoboWebCompiler\Source\Render\InterFontData.cpp"


def main():
    print("downloading", URL, flush=True)
    data = urllib.request.urlopen(URL, timeout=180).read()
    print("zip", len(data), flush=True)
    archive = zipfile.ZipFile(io.BytesIO(data))
    with open(OUTPUT, "w", newline="\n", encoding="ascii") as output:
        output.write('#include "Render/InterFontData.h"\n\nnamespace RoboWeb {\n\n')
        for name, symbol in WANTED:
            blob = archive.read(name)
            output.write("const unsigned char %s[] = {\n" % symbol)
            for index in range(0, len(blob), 20):
                chunk = blob[index : index + 20]
                output.write("    " + ",".join(str(byte) for byte in chunk) + ",\n")
            output.write("};\n\nconst std::size_t %sSize = sizeof(%s);\n\n" % (symbol, symbol))
            print(symbol, len(blob), flush=True)
        output.write("}\n")
    print("wrote", OUTPUT, flush=True)


if __name__ == "__main__":
    main()
