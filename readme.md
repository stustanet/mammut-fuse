MammutFS
========

Das Mammut File System stellt die Daten auf Mammut so als Mount-Points zur
Verfügung, dass sie von den verschiedenen Diensten (SMB, FTP, WWW, SSHFS, ...)
direkt genutzt werden können.

Ausgangsstruktur
-----------------

Die Daten sollen auf der Festplatte ähnlich der folgenden Struktur sein:

```
    raids
    ├── raid0
    │   ├── anonymous
    │   │   ├── 001001
    │   │   ├── 001002
    |   ├── private
    │   │   ├── 001001
    |   │   ├── 001002
    |   │   └── 001003
    │   │       ├── bar_001003
    │   │       ├── baz_001003
    │   │       ├── foo_001003
    │   │       └── trolling_001003
    │   └── public
    │       ├── 001001
    │       ├── 001002
    │       └── 001003
    ├── raid1
    │   ├── private
    │   └── public
    └── raid2
    ├── backup
    │   ├── 003001
    │   ├── 003002
    │   └── 003003
```

Die Raid-Ordner, die zu durchsuchen sind, werden im Configfile aufgelistet.

Die darin lagernden Daten sind in Module aufgegliedert. Diese sind public,
private, anonymous, backup und lister. Diese Module sind auch als Unterordner
in den Raids wieder aufzufinden. Innerhalb dieser Unterordner befindet sich
jeweils das User-Verzeichnis.

Konfiguration
-------------

Konfiguriert wird das FS über das Configfile mammutfs.cfg und über parameter.
Alle Optionen im Configfile können mithilfe des Kommandozeilenparameters
überschrieben werden.
Ein typischer Start von Mammutfs würde demnach so aussehen, um es für den User
001001 zu starten:

```
$ mammutfs --username 001001
```

Anschließend ist im Mount-Directory ein neuer folder für diesen Nutzer, mit allen
Unterordnern (intern als Module bezeichnet).

Unix-Socket
-----------

Außerdem stellt jedes gestartete Mammutfs einen Unix-Socket bereit, durch den
Befehle an das FS geschickt werden können um zur Laufzeit beispielsweise ein
Neueinlesen des Anonymous-Mapping-Files zu veranlassen, den Cache zu leeren oder
andere Befehle. Eine Auflistung aller aktiven Befehle bekommt man, wenn man
"HELP" an das FS schickt.

Ein Referenz-Client in Python liegt vor.



