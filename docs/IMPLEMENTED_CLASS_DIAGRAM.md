# Implemented class diagram

The PlantUML source for the current implementation is
[`IMPLEMENTED_CLASS_DIAGRAM.puml`](IMPLEMENTED_CLASS_DIAGRAM.puml).

This diagram describes the current prototype, not the proposed end-state in
[`references/diagrams/class.jpeg`](../references/diagrams/class.jpeg). It
includes the implemented `MainWindow` class and `InspectionReport`, plus the
current helper structs and important free functions. The free-function group
in the diagram is explicitly shown as a package, not as a made-up C++ class.

The GUI currently starts the CLI through `QProcess`; it does not yet consume
`InspectionReport` directly. Parser classes such as `XFSParser` and
`BtrfsParser`, recovery verification, recovered-file records, and storage or
report-export classes shown in the supplied diagram remain planned work.

To render the PlantUML source, use PlantUML with Graphviz installed:

```bash
plantuml -tpng docs/IMPLEMENTED_CLASS_DIAGRAM.puml
plantuml -tpdf docs/IMPLEMENTED_CLASS_DIAGRAM.puml
```
