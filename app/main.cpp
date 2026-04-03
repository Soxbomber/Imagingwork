#include "Imagingwork.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Imagingwork window;
    window.show();
    return app.exec();
}
