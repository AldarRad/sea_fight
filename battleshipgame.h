// battleshipgame.h
#ifndef BATTLESHIPGAME_H
#define BATTLESHIPGAME_H

#include <QGraphicsView>
#include <QMouseEvent>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <vector>
#include <QDialog>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QCheckBox>
#include <QPushButton>
#include <QtMultimedia/QSoundEffect>
#include <QHBoxLayout>

enum GameSize { Size8x8 = 8, Size10x10 = 10, Size12x12 = 12 };

const int DEFAULT_CELL_SIZE = 40;
const int WINDOW_WIDTH = DEFAULT_CELL_SIZE * Size10x10 * 2 + 300;
const int WINDOW_HEIGHT = DEFAULT_CELL_SIZE * Size10x10 + 400;

const QColor COLOR_BACKGROUND(20, 20, 40);
const QColor COLOR_GRID(30, 30, 60);
const QColor COLOR_SHIP(80, 160, 80);
const QColor COLOR_HIT(200, 80, 80);
const QColor COLOR_MISS(200, 200, 200);
const QColor COLOR_TEXT(240, 240, 240);
const QColor COLOR_TITLE(255, 215, 0);
const QColor COLOR_PREVIEW(255, 255, 0, 150);
const QColor COLOR_BLOCKED(255, 0, 0, 100);
const QColor COLOR_PLAYER_LABEL(100, 200, 255);
const QColor COLOR_AI_LABEL(255, 100, 150);
const QColor COLOR_SHIP_COUNT(220, 220, 220);
const QColor COLOR_GRID_LABELS(180, 180, 255);
const QColor COLOR_WAITING(255, 165, 0);
const QColor COLOR_MINE(255, 165, 0, 150);

enum Cell { Empty, Ship, Hit, Miss, Mine };

struct ShipInfo {
    int size;
    int count;
    int remaining;
    QString name;
};

using Grid = std::vector<std::vector<Cell>>;

class BattleShipGame : public QGraphicsView {
    Q_OBJECT
public:
    enum GameSize { Size8x8 = 8, Size10x10 = 10, Size12x12 = 12 };
    static const int DEFAULT_CELL_SIZE = 40;
    BattleShipGame(QWidget *parent = nullptr);
    void initializeGame();
    ~BattleShipGame();

protected:
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void hideMessage();
    void cancelConnection();
    void newConnection();
    void readData();
    void disconnected();
    void connectionError(QAbstractSocket::SocketError socketError);

private:
    int lastShotX = -1;
    int lastShotY = -1;
    bool mineExploded = false;
    QSoundEffect hitSound;
    QSoundEffect missSound;
    QSoundEffect winSound;
    QSoundEffect loseSound;
    QGraphicsScene *scene;
    Grid playerGrid;
    Grid opponentGrid;
    std::vector<ShipInfo> playerFleet;
    std::vector<ShipInfo> opponentFleet;
    bool placing;
    bool horizontal;
    int currentShipIndex;
    bool myTurn;
    bool gameEnded;
    QTimer *messageTimer;
    QString currentMessage;
    int gridSize;
    int cellSize;
    bool minesEnabled;
    int minesCount;

    QTcpServer *server;
    QTcpSocket *socket;
    bool isServer;

    void initializeFleet();
    void placeMines(Grid& grid);
    void sendMessage(const QString &message);
    void processCommand(const QString &command, const QString &data);
    bool isInside(int x, int y);
    bool isSurroundingClear(Grid& grid, int x, int y, int size, bool horizontal);
    bool canPlace(Grid& grid, int x, int y, int size, bool horizontal);
    void placeShip(Grid& grid, int x, int y, int size, bool horizontal);
    std::vector<std::pair<int, int>> getShipCells(const Grid& grid, int x, int y);
    bool isShipSunk(const Grid& grid, int x, int y);
    void drawGrid(int offsetX, int offsetY, const Grid& grid,
                  const std::vector<ShipInfo>& fleetInfo, bool showShips, const QString& label);
    void drawGrids();
    void setupOpponentGrid();
    bool isGameOver(const std::vector<ShipInfo>& fleetInfo);
    void showMessage(const QString& message, bool timeout = true);
    void startNetworkGame(bool asServer);
    void endGame(bool winner);
    void showGameOptions();
};

#endif // BATTLESHIPGAME_H
