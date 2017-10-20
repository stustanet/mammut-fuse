(TeX-add-style-hook
 "main"
 (lambda ()
   (TeX-run-style-hooks
    "latex2e"
    "beamer"
    "beamer10"
    "tikz"
    "listings")
   (TeX-add-symbols
    '("raidbox" 5)
    '("tikzbox" 5)))
 :latex)

