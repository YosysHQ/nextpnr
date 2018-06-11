# Qt Solutions Component: Property Browser

A property browser framework enabling the user to edit a set of properties.

The framework provides a browser widget that displays the given properties with labels and corresponding editing widgets (e.g. line edits or comboboxes). The various types of editing widgets are provided by the framework's editor factories: For each property type, the framework provides a property manager (e.g. QtIntPropertyManager and QtStringPropertyManager) which can be associated with the preferred editor factory (e.g.QtSpinBoxFactory and QtLineEditFactory). The framework also provides a variant based property type with corresponding variant manager and factory. Finally, the framework provides three ready-made implementations of the browser widget: QtTreePropertyBrowser, QtButtonPropertyBrowser and QtGroupBoxPropertyBrowser.

Original source code is archived at https://qt.gitorious.org/qt-solutions/qt-solutions
This fork adds CMake and Qt5 support