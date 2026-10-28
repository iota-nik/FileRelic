# FileRelic class diagrams

> **Historical snapshot:** These rendered diagrams predate the latest shared-core
> change. In particular, the old implemented PNG still shows a `QProcess`
> connection that has been removed from the code. Nikunj asked to redraw the
> diagrams after the architecture settles, so treat the PlantUML and rendered
> files below as archived project history rather than current implementation
> documentation. The current source and [session handoff](SESSION_HANDOFF.md)
> describe the live architecture.

This document keeps two diagrams with different purposes. The supplied class
diagram is the target architecture; the implemented diagram records what the
current source code actually contains.

## Implemented code

[`IMPLEMENTED_CLASS_DIAGRAM.puml`](IMPLEMENTED_CLASS_DIAGRAM.puml) describes the
current implementation and uses the same left-to-right layered flow as the
supplied [`class.jpeg`](../references/diagrams/class.jpeg). It includes the
actual Qt `MainWindow`, `InspectionReport`, helper structs, and the current
inspection functions. Free-function groups are explicitly labeled because
those operations have not yet been converted into parser classes.

Rendered files:

- [PNG](FileRelicImplementedArchitecture.png)
- [PDF](FileRelicImplementedArchitecture.pdf)

The GUI currently starts the CLI process and formats its output into readable
cards. Although an `InspectionReport` type exists, the GUI does not yet use it
directly; this connection is shown honestly in the diagram.

## Target architecture in the supplied reference

[`PROPOSED_CLASS_DIAGRAM.puml`](PROPOSED_CLASS_DIAGRAM.puml) reproduces the
classes, methods, attributes, inheritance, and relationships shown in
[`class.jpeg`](../references/diagrams/class.jpeg). It is the intended design,
not a claim that each class has already been implemented.

Rendered files:

- [PNG](FileRelicProposedArchitecture.png)
- [PDF](FileRelicProposedArchitecture.pdf)

## Regenerate the diagrams

Run these commands from the repository root after installing PlantUML and
Graphviz:

```bash
plantuml -tpng docs/IMPLEMENTED_CLASS_DIAGRAM.puml
plantuml -tpdf docs/IMPLEMENTED_CLASS_DIAGRAM.puml
plantuml -tpng docs/PROPOSED_CLASS_DIAGRAM.puml
plantuml -tpdf docs/PROPOSED_CLASS_DIAGRAM.puml
```
