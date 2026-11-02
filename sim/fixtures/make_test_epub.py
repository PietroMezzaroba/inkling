#!/usr/bin/env python3
"""Builds a small EPUB with original Italian text for simulator screenshots.

Usage: make_test_epub.py OUT.epub
"""
import sys
import zipfile

CHAPTERS = [
    ("Il lettore che non dormiva", [
        "Ogni sera, quando la casa si faceva silenziosa, il piccolo lettore di carta "
        "elettronica restava acceso ancora un momento. Non per pigrizia, diceva lui, "
        "ma per <i>prudenza</i>: qualcuno poteva sempre avere voglia di un'ultima pagina.",
        "Sullo schermo, in un angolo, viveva una creatura minuscola. Non aveva ancora un "
        "nome. Mangiava minuti di lettura, si annoiava se il libro restava chiuso troppo "
        "a lungo e, quando la padrona finiva un capitolo, faceva una piccola capriola.",
        "Il primo giorno la creatura era soltanto un uovo. Il secondo giorno l'uovo "
        "tremò. Il terzo giorno, dopo quaranta pagine lette di fila, si aprì con un "
        "rumore che nessuno sentì, perché la carta elettronica non fa rumore.",
        "Da allora la creatura imparò tre cose: che le pagine si girano sempre in "
        "avanti, tranne quando si cerca una frase perduta; che la batteria è un "
        "tesoro da non sprecare; e che i libri più belli sono quelli che non si "
        "riesce a chiudere.",
    ]),
    ("La montagna di carta", [
        "C'era una volta una montagna fatta di libri. Chi voleva salirci doveva "
        "leggerne uno per ogni gradino, e i gradini erano migliaia. Pochi arrivavano "
        "in cima, ma nessuno tornava indietro uguale a prima.",
        "La creatura guardava la montagna dal suo angolo di schermo e faceva i conti: "
        "<b>trenta minuti</b> al giorno, una settimana dopo l'altra, e forse un giorno "
        "anche lei avrebbe visto la vetta. Intanto si accontentava del primo rifugio, "
        "dove si mangiavano parole calde e si dormiva tra le virgole.",
        "Certe notti il vento della montagna sfogliava le pagine da solo. Allora la "
        "creatura si svegliava di soprassalto, controllava il segnalibro e si "
        "riaddormentava tranquilla: il punto esatto era salvato, carattere per "
        "carattere, anche se la corrente fosse mancata all'improvviso.",
        "È così che funzionano le cose ben fatte: non le noti finché tutto va bene, "
        "e ti salvano proprio quando qualcosa va storto.",
    ]),
    ("Epilogo", [
        "Questa è una storia di prova. Serve soltanto a vedere come appaiono le "
        "pagine: le lettere accentate (à, è, é, ì, ò, ù), il <i>corsivo</i>, il "
        "<b>grassetto</b>, i paragrafi lunghi e quelli brevi.",
        "Fine.",
    ]),
]

CONTAINER = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>"""


def chapter_xhtml(title, paragraphs):
    body = "\n".join(f"<p>{p}</p>" for p in paragraphs)
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="it">
<head><title>{title}</title><link rel="stylesheet" href="style.css"/></head>
<body><h1>{title}</h1>
{body}
</body></html>"""


def main():
    out = sys.argv[1]
    n = len(CHAPTERS)
    manifest = "\n".join(
        f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(n))
    spine = "\n".join(f'<itemref idref="c{i}"/>' for i in range(n))
    navpoints = "\n".join(
        f'<navPoint id="n{i}" playOrder="{i + 1}"><navLabel><text>{t}</text></navLabel>'
        f'<content src="c{i}.xhtml"/></navPoint>' for i, (t, _) in enumerate(CHAPTERS))
    opf = f"""<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>Prova di Inkling</dc:title>
    <dc:creator>Inkling</dc:creator>
    <dc:language>it</dc:language>
    <dc:identifier id="id">inkling-test-1</dc:identifier>
  </metadata>
  <manifest>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="css" href="style.css" media-type="text/css"/>
    {manifest}
  </manifest>
  <spine toc="ncx">{spine}</spine>
</package>"""
    ncx = f"""<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="inkling-test-1"/></head>
  <docTitle><text>Prova di Inkling</text></docTitle>
  <navMap>{navpoints}</navMap>
</ncx>"""
    css = "h1 { text-align: center; margin-bottom: 1em; } p { text-indent: 1.2em; margin: 0; }"

    with zipfile.ZipFile(out, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", CONTAINER, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", opf, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/toc.ncx", ncx, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/style.css", css, compress_type=zipfile.ZIP_DEFLATED)
        for i, (title, paras) in enumerate(CHAPTERS):
            z.writestr(f"OEBPS/c{i}.xhtml", chapter_xhtml(title, paras), compress_type=zipfile.ZIP_DEFLATED)


if __name__ == "__main__":
    main()
