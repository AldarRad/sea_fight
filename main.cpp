#include "battleshipgame.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    BattleShipGame game;
    game.show();

    return app.exec();
}
