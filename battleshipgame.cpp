#include "battleshipgame.h"
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QMouseEvent>
#include <QFont>
#include <QMessageBox>
#include <QBrush>
#include <QPen>
#include <QColor>
#include <QInputDialog>
#include <QHostAddress>
#include <ctime>
#include <cstdlib>
#include <utility>

static std::vector<ShipInfo> fleet = {
    {4, 1, 1, "Линкор"},
    {3, 2, 2, "Крейсер"},
    {2, 3, 3, "Эсминец"},
    {1, 4, 4, "Катер"}
};

BattleShipGame::BattleShipGame(QWidget *parent) : QGraphicsView(parent),
    placing(true), horizontal(true), currentShipIndex(0), myTurn(false), gameEnded(false),
    server(nullptr), socket(nullptr), isServer(false) {

    scene = new QGraphicsScene(this);
    setScene(scene);
    setFixedSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    setBackgroundBrush(QBrush(Qt::black));

    setFocusPolicy(Qt::StrongFocus);
    setFocus();
    setMouseTracking(true);

    playerGrid.resize(GRID_SIZE, std::vector<Cell>(GRID_SIZE, Empty));
    opponentGrid.resize(GRID_SIZE, std::vector<Cell>(GRID_SIZE, Empty));

    playerFleet = fleet;
    opponentFleet = fleet;

    messageTimer = new QTimer(this);
    connect(messageTimer, &QTimer::timeout, this, &BattleShipGame::hideMessage);

    // Запрос у пользователя, хочет ли он создать игру или подключиться
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Сетевая игра",
        "Хотите создать игру (сервер) или подключиться к существующей (клиент)?",
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel
        );

    if (reply == QMessageBox::Yes) {
        startNetworkGame(true); // Сервер
    } else if (reply == QMessageBox::No) {
        startNetworkGame(false); // Клиент
    } else {
        QCoreApplication::quit();
        return;
    }

    drawGrids();
}

BattleShipGame::~BattleShipGame() {
    if (server) server->close();
    if (socket) socket->close();
    delete server;
    delete socket;
}

void BattleShipGame::startNetworkGame(bool asServer) {
    isServer = asServer;

    if (asServer) {
        server = new QTcpServer(this);
        if (!server->listen(QHostAddress::Any, 12345)) {
            QMessageBox::critical(this, "Ошибка", "Не удалось запустить сервер: " + server->errorString());
            QCoreApplication::quit();
            return;
        }
        connect(server, &QTcpServer::newConnection, this, &BattleShipGame::newConnection);
        showMessage("Ожидание подключения игрока...", false);
    } else {
        bool ok;
        QString host = QInputDialog::getText(
            this, "Подключение к серверу",
            "Введите IP-адрес сервера:", QLineEdit::Normal,
            "127.0.0.1", &ok
            );

        if (!ok) {
            QCoreApplication::quit();
            return;
        }

        socket = new QTcpSocket(this);
        connect(socket, &QTcpSocket::connected, this, [this]() {
            showMessage("Подключено к серверу. Ожидаем расстановки кораблей...", false);
        });
        connect(socket, &QTcpSocket::readyRead, this, &BattleShipGame::readData);
        connect(socket, &QTcpSocket::disconnected, this, &BattleShipGame::disconnected);
        connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                this, &BattleShipGame::connectionError);

        socket->connectToHost(host, 12345);
    }
}

void BattleShipGame::sendMessage(const QString &message) {
    if (socket && socket->state() == QAbstractSocket::ConnectedState) {
        QByteArray data = message.toUtf8();
        socket->write(data);
        socket->flush();
    }
}

void BattleShipGame::processCommand(const QString &command, const QString &data) {
    if (command == "SHOT") {
        QStringList coords = data.split(',');
        if (coords.size() == 2) {
            int x = coords[0].toInt();
            int y = coords[1].toInt();

            if (playerGrid[x][y] == Ship) {
                playerGrid[x][y] = Hit;

                if (isShipSunk(playerGrid, x, y)) {
                    auto cells = getShipCells(playerGrid, x, y);
                    for (auto& s : playerFleet) {
                        if (s.remaining > 0 && s.size == (int)cells.size()) {
                            s.remaining--;
                            sendMessage("SUNK:" + QString::number(s.size));
                            showMessage("Противник потопил ваш " + s.name + "!");
                            break;
                        }
                    }
                } else {
                    sendMessage("HIT:" + data);
                }

                if (isGameOver(playerFleet)) {
                    endGame(false);
                    return;
                }
            } else if (playerGrid[x][y] == Empty) {
                playerGrid[x][y] = Miss;
                sendMessage("MISS:" + data);
                myTurn = true;
                showMessage("Ваш ход!", false);
            }
        }
    }
    else if (command == "HIT") {
        QStringList coords = data.split(',');
        if (coords.size() == 2) {
            int x = coords[0].toInt();
            int y = coords[1].toInt();
            opponentGrid[x][y] = Hit;
            showMessage("Вы попали!", true);
        }
    }
    else if (command == "MISS") {
        QStringList coords = data.split(',');
        if (coords.size() == 2) {
            int x = coords[0].toInt();
            int y = coords[1].toInt();
            opponentGrid[x][y] = Miss;
            showMessage("Вы промахнулись!", true);
            myTurn = false;
            showMessage("Ожидаем ход противника...", false);
        }
    }
    else if (command == "SUNK") {
        int size = data.toInt();
        for (auto& s : opponentFleet) {
            if (s.size == size && s.remaining > 0) {
                s.remaining--;
                showMessage("Вы потопили " + s.name + " противника!", true);
                break;
            }
        }

        if (isGameOver(opponentFleet)) {
            endGame(true);
        }
    }
    else if (command == "READY") {
        if (isServer) {
            myTurn = true;
            showMessage("Игра началась! Ваш ход.", false);
        } else {
            myTurn = false;
            showMessage("Игра началась! Ожидаем ход противника...", false);
        }
    }

    drawGrids();
}

void BattleShipGame::newConnection() {
    if (server && server->hasPendingConnections()) {
        socket = server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &BattleShipGame::readData);
        connect(socket, &QTcpSocket::disconnected, this, &BattleShipGame::disconnected);
        connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                this, &BattleShipGame::connectionError);

        showMessage("Игрок подключен! Расставьте корабли.", false);
    }
}

void BattleShipGame::readData() {
    while (socket && socket->canReadLine()) {
        QString message = QString::fromUtf8(socket->readLine()).trimmed();
        QStringList parts = message.split(':');
        if (parts.size() >= 2) {
            processCommand(parts[0], parts[1]);
        }
    }
}

void BattleShipGame::disconnected() {
    showMessage("Соединение разорвано. Игра завершена.", false);
    gameEnded = true;
    if (socket) socket->deleteLater();
    if (server) server->close();
    socket = nullptr;
}

void BattleShipGame::connectionError(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError);
    if (socket) {
        showMessage("Ошибка соединения: " + socket->errorString(), false);
    }
}

void BattleShipGame::mouseMoveEvent(QMouseEvent *event) {
    if (placing && currentShipIndex < playerFleet.size()) {
        drawGrids();
    }
    QGraphicsView::mouseMoveEvent(event);
}

bool BattleShipGame::isInside(int x, int y) {
    return x >= 0 && y >= 0 && x < GRID_SIZE && y < GRID_SIZE;
}

bool BattleShipGame::isSurroundingClear(Grid& grid, int x, int y, int size, bool horizontal) {
    for (int i = -1; i <= size; ++i) {
        for (int j = -1; j <= 1; ++j) {
            int nx = x + (horizontal ? i : j);
            int ny = y + (horizontal ? j : i);
            if (isInside(nx, ny) && grid[nx][ny] != Empty)
                return false;
        }
    }
    return true;
}

bool BattleShipGame::canPlace(Grid& grid, int x, int y, int size, bool horizontal) {
    for (int i = 0; i < size; ++i) {
        int px = x + (horizontal ? i : 0);
        int py = y + (horizontal ? 0 : i);
        if (!isInside(px, py) || grid[px][py] != Empty)
            return false;
    }
    return isSurroundingClear(grid, x, y, size, horizontal);
}

void BattleShipGame::placeShip(Grid& grid, int x, int y, int size, bool horizontal) {
    for (int i = 0; i < size; ++i) {
        int px = x + (horizontal ? i : 0);
        int py = y + (horizontal ? 0 : i);
        grid[px][py] = Ship;
    }
}

std::vector<std::pair<int, int>> BattleShipGame::getShipCells(const Grid& grid, int x, int y) {
    std::vector<std::pair<int, int>> cells;
    if (grid[x][y] != Hit) return cells;

    int startX = x;
    while (startX > 0 && grid[startX - 1][y] == Hit) --startX;
    int endX = x;
    while (endX + 1 < GRID_SIZE && grid[endX + 1][y] == Hit) ++endX;
    if (endX - startX >= 1) {
        for (int i = startX; i <= endX; ++i) cells.emplace_back(i, y);
        return cells;
    }

    int startY = y;
    while (startY > 0 && grid[x][startY - 1] == Hit) --startY;
    int endY = y;
    while (endY + 1 < GRID_SIZE && grid[x][endY + 1] == Hit) ++endY;
    if (endY - startY >= 1) {
        for (int i = startY; i <= endY; ++i) cells.emplace_back(x, i);
        return cells;
    }

    cells.emplace_back(x, y);
    return cells;
}

bool BattleShipGame::isShipSunk(const Grid& grid, int x, int y) {
    auto cells = getShipCells(grid, x, y);
    for (auto& [cx, cy] : cells) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                if (abs(dx) + abs(dy) != 1) continue;
                int nx = cx + dx, ny = cy + dy;
                if (isInside(nx, ny) && grid[nx][ny] == Ship)
                    return false;
            }
        }
    }
    return true;
}

void BattleShipGame::drawGrid(int offsetX, int offsetY, const Grid& grid,
                              const std::vector<ShipInfo>& fleetInfo, bool showShips, const QString& label) {
    QFont font("Arial", 14, QFont::Bold);
    QFont smallFont("Arial", 10);

    // Заголовок поля
    QGraphicsTextItem *gridLabel = new QGraphicsTextItem(label);
    gridLabel->setFont(QFont("Arial", 16, QFont::Bold));
    gridLabel->setDefaultTextColor(label == "Игрок" ? COLOR_PLAYER_LABEL : COLOR_AI_LABEL);
    gridLabel->setPos(offsetX, offsetY - 70);
    scene->addItem(gridLabel);

    // Буквы (A-J) сверху
    for (int x = 0; x < GRID_SIZE; ++x) {
        QGraphicsTextItem *letter = new QGraphicsTextItem(QString(QChar('A' + x)));
        letter->setFont(font);
        letter->setDefaultTextColor(COLOR_GRID_LABELS);
        letter->setPos(offsetX + x * CELL_SIZE + CELL_SIZE/2 - 7, offsetY - 40);
        scene->addItem(letter);
    }

    // Цифры (1-10) слева
    for (int y = 0; y < GRID_SIZE; ++y) {
        QGraphicsTextItem *number = new QGraphicsTextItem(QString::number(y + 1));
        number->setFont(font);
        number->setDefaultTextColor(COLOR_GRID_LABELS);
        number->setPos(offsetX - 30, offsetY + y * CELL_SIZE + CELL_SIZE/2 - 10);
        scene->addItem(number);
    }

    // Рамка вокруг поля
    QGraphicsRectItem *border = new QGraphicsRectItem(
        offsetX - 3, offsetY - 3,
        GRID_SIZE * CELL_SIZE + 6, GRID_SIZE * CELL_SIZE + 6);
    QPen borderPen(label == "Игрок" ? COLOR_PLAYER_LABEL : COLOR_AI_LABEL, 3);
    borderPen.setJoinStyle(Qt::MiterJoin);
    border->setPen(borderPen);
    border->setBrush(Qt::NoBrush);
    scene->addItem(border);

    // Отрисовка клеток
    for (int x = 0; x < GRID_SIZE; ++x) {
        for (int y = 0; y < GRID_SIZE; ++y) {
            QGraphicsRectItem *cell = new QGraphicsRectItem(1, 1, CELL_SIZE - 4, CELL_SIZE - 4);
            cell->setPos(offsetX + x * CELL_SIZE + 1, offsetY + y * CELL_SIZE + 1);
            cell->setPen(Qt::NoPen);

            if (grid[x][y] == Ship && !showShips) {
                QLinearGradient grad(0, 0, 0, CELL_SIZE);
                grad.setColorAt(0, QColor(30, 60, 120));
                grad.setColorAt(1, QColor(10, 30, 80));
                cell->setBrush(grad);
            }
            else if (grid[x][y] == Ship) {
                cell->setBrush(QBrush(COLOR_SHIP));
            }
            else if (grid[x][y] == Hit) {
                QRadialGradient grad(cell->rect().center(), CELL_SIZE/2);
                grad.setColorAt(0, QColor(255, 80, 80));
                grad.setColorAt(1, QColor(180, 20, 20));
                cell->setBrush(grad);
            }
            else if (grid[x][y] == Miss) {
                cell->setBrush(QBrush(COLOR_MISS));
            }
            else {
                QLinearGradient grad(0, 0, 0, CELL_SIZE);
                grad.setColorAt(0, QColor(30, 60, 120));
                grad.setColorAt(1, QColor(10, 30, 80));
                cell->setBrush(grad);
            }
            scene->addItem(cell);
        }
    }

    // Отрисовка информации о флоте
    QGraphicsRectItem *fleetBg = new QGraphicsRectItem(
        offsetX - 10, offsetY + GRID_SIZE * CELL_SIZE + 15,
        GRID_SIZE * CELL_SIZE + 20, fleetInfo.size() * 25 + 30);
    fleetBg->setBrush(QBrush(QColor(20, 40, 80, 200)));
    fleetBg->setPen(QPen(label == "Игрок" ? COLOR_PLAYER_LABEL : COLOR_AI_LABEL, 2));
    scene->addItem(fleetBg);

    for (size_t i = 0; i < fleetInfo.size(); ++i) {
        QGraphicsRectItem *shipIcon = new QGraphicsRectItem(
            offsetX, offsetY + GRID_SIZE * CELL_SIZE + 30 + i * 25,
            15, 15);
        shipIcon->setBrush(fleetInfo[i].remaining == 0 ? QBrush(Qt::gray) :
                               (label == "Игрок" ? QBrush(COLOR_PLAYER_LABEL) : QBrush(COLOR_AI_LABEL)));
        scene->addItem(shipIcon);

        QGraphicsTextItem *text = new QGraphicsTextItem(
            QString("  %1: %2/%3").arg(fleetInfo[i].name)
                .arg(fleetInfo[i].remaining)
                .arg(fleetInfo[i].count));
        text->setFont(smallFont);
        text->setPos(offsetX + 20, offsetY + GRID_SIZE * CELL_SIZE + 25 + i * 25);
        text->setDefaultTextColor(fleetInfo[i].remaining == 0 ? Qt::gray : COLOR_SHIP_COUNT);
        scene->addItem(text);
    }
}

void BattleShipGame::drawGrids() {
    scene->clear();

    drawGrid(50, 50, playerGrid, playerFleet, true, "Игрок");
    drawGrid(50 + GRID_SIZE * CELL_SIZE + 50, 50, opponentGrid, opponentFleet, false, "Противник");

    // Показ превью текущего корабля при расстановке
    if (placing && currentShipIndex < playerFleet.size()) {
        QPoint mousePos = mapFromGlobal(QCursor::pos());
        QPointF pos = mapToScene(mousePos);
        int mx = (pos.x() - 50) / CELL_SIZE;
        int my = (pos.y() - 50) / CELL_SIZE;

        if (mx >= 0 && mx < GRID_SIZE && my >= 0 && my < GRID_SIZE) {
            auto& ship = playerFleet[currentShipIndex];
            if (ship.count > 0) {
                bool canPlaceHere = canPlace(playerGrid, mx, my, ship.size, horizontal);
                QColor previewColor = canPlaceHere ? Qt::yellow : Qt::red;

                // Рисуем контур корабля
                for (int i = 0; i < ship.size; ++i) {
                    int px = mx + (horizontal ? i : 0);
                    int py = my + (horizontal ? 0 : i);
                    if (px < GRID_SIZE && py < GRID_SIZE) {
                        QGraphicsRectItem *outline = new QGraphicsRectItem(0, 0, CELL_SIZE - 2, CELL_SIZE - 2);
                        outline->setPos(50 + px * CELL_SIZE, 50 + py * CELL_SIZE);
                        outline->setBrush(Qt::NoBrush);
                        outline->setPen(QPen(previewColor, 2));
                        scene->addItem(outline);
                    }
                }

                // Рисуем красную зону вокруг корабля
                if (!canPlaceHere) {
                    for (int i = -1; i <= ship.size; ++i) {
                        for (int j = -1; j <= 1; ++j) {
                            int px = mx + (horizontal ? i : j);
                            int py = my + (horizontal ? j : i);
                            if (isInside(px, py) && playerGrid[px][py] == Empty) {
                                QGraphicsRectItem *blocked = new QGraphicsRectItem(0, 0, CELL_SIZE - 2, CELL_SIZE - 2);
                                blocked->setPos(50 + px * CELL_SIZE, 50 + py * CELL_SIZE);
                                blocked->setBrush(QBrush(QColor(255, 0, 0, 100)));
                                scene->addItem(blocked);
                            }
                        }
                    }
                }

                // Подпись с названием и размером корабля
                QGraphicsTextItem *shipInfo = new QGraphicsTextItem(ship.name + " (" + QString::number(ship.size) + " клетки)");
                shipInfo->setFont(QFont("Arial", 12));
                shipInfo->setDefaultTextColor(Qt::white);
                shipInfo->setPos(50, 6);
                scene->addItem(shipInfo);

                // Подсказка управления
                QGraphicsTextItem *hint = new QGraphicsTextItem("Нажмите X для смены ориентации");
                hint->setFont(QFont("Arial", 10));
                hint->setDefaultTextColor(Qt::white);
                hint->setPos(50, 50 + GRID_SIZE * CELL_SIZE + 200);
                scene->addItem(hint);
            }
        }
    }

    // Сообщение о состоянии игры
    if (!currentMessage.isEmpty()) {
        QGraphicsTextItem *msg = new QGraphicsTextItem(currentMessage);
        msg->setFont(QFont("Arial", 16, QFont::Bold));
        msg->setDefaultTextColor(COLOR_WAITING);
        QRectF rect = msg->boundingRect();
        msg->setPos(WINDOW_WIDTH/2 - rect.width()/2, WINDOW_HEIGHT - 50);

        // Фон для сообщения
        QGraphicsRectItem *msgBg = new QGraphicsRectItem(
            WINDOW_WIDTH/2 - rect.width()/2 - 10,
            WINDOW_HEIGHT - 50 - 5,
            rect.width() + 20,
            rect.height() + 10
            );
        msgBg->setBrush(QBrush(Qt::black));
        msgBg->setPen(QPen(COLOR_WAITING, 2));
        scene->addItem(msgBg);
        scene->addItem(msg);
    }
}

void BattleShipGame::setupOpponentGrid() {
    // В сетевой версии мы не знаем расположение кораблей противника
    // Просто очищаем поле
    for (auto& row : opponentGrid) {
        std::fill(row.begin(), row.end(), Empty);
    }
}

bool BattleShipGame::isGameOver(const std::vector<ShipInfo>& fleetInfo) {
    for (const auto& s : fleetInfo) {
        if (s.remaining > 0) return false;
    }
    return true;
}

void BattleShipGame::showMessage(const QString& message, bool timeout) {
    currentMessage = message;
    drawGrids();

    if (timeout) {
        messageTimer->start(3000);
    }
}

void BattleShipGame::hideMessage() {
    messageTimer->stop();
    currentMessage.clear();
    drawGrids();
}

void BattleShipGame::endGame(bool winner) {
    gameEnded = true;
    if (winner) {
        showMessage("Поздравляем! Вы выиграли!", false);
    } else {
        showMessage("К сожалению, вы проиграли...", false);
    }
}

void BattleShipGame::mousePressEvent(QMouseEvent *event) {
    if (gameEnded) return;

    if (placing) {
        if (event->button() == Qt::LeftButton && currentShipIndex < playerFleet.size()) {
            QPointF pos = mapToScene(event->pos());
            int mx = (pos.x() - 50) / CELL_SIZE;
            int my = (pos.y() - 50) / CELL_SIZE;

            if (mx >= 0 && mx < GRID_SIZE && my >= 0 && my < GRID_SIZE) {
                auto& ship = playerFleet[currentShipIndex];
                if (ship.count > 0 && canPlace(playerGrid, mx, my, ship.size, horizontal)) {
                    placeShip(playerGrid, mx, my, ship.size, horizontal);
                    ship.count--;
                    if (ship.count == 0) currentShipIndex++;
                    if (currentShipIndex >= playerFleet.size()) {
                        placing = false;
                        if (socket && socket->state() == QAbstractSocket::ConnectedState) {
                            sendMessage("READY:");
                            if (isServer) {
                                myTurn = true;
                                showMessage("Игра началась! Ваш ход.", false);
                            } else {
                                myTurn = false;
                                showMessage("Игра началась! Ожидаем ход противника...", false);
                            }
                        }
                    }
                    drawGrids();
                }
            }
        }
    } else if (myTurn && !placing) {
        if (event->button() == Qt::LeftButton) {
            QPointF pos = mapToScene(event->pos());
            int mx = (pos.x() - (50 + GRID_SIZE * CELL_SIZE + 50)) / CELL_SIZE;
            int my = (pos.y() - 50) / CELL_SIZE;

            if (mx >= 0 && mx < GRID_SIZE && my >= 0 && my < GRID_SIZE) {
                if (opponentGrid[mx][my] == Empty) {
                    sendMessage("SHOT:" + QString::number(mx) + "," + QString::number(my));
                    myTurn = false;
                    showMessage("Ожидаем ответ противника...", false);
                }
            }
        }
    }
}
void BattleShipGame::cancelConnection() {
    if (socket) {
        socket->abort();  // Принудительно разрываем соединение
        socket->deleteLater();
        socket = nullptr;
    }
    if (server) {
        server->close();
        server->deleteLater();
        server = nullptr;
    }
    showMessage("Подключение отменено", true);
    placing = true;  // Возвращаем в состояние расстановки кораблей
}
void BattleShipGame::keyPressEvent(QKeyEvent *event) {
    if ((event->key() == Qt::Key_X || event->key() == 1063) && placing) {
        horizontal = !horizontal;
        drawGrids();
    } else {
        QGraphicsView::keyPressEvent(event);
    }
}
