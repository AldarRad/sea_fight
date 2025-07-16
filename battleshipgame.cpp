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
#include <random>

BattleShipGame::BattleShipGame(QWidget *parent) : QGraphicsView(parent),
    placing(true), horizontal(true), currentShipIndex(0), myTurn(false),
    gameEnded(false), server(nullptr), socket(nullptr), isServer(false),
    gridSize(Size10x10), cellSize(DEFAULT_CELL_SIZE), minesEnabled(false),
    minesCount(2)
{
    scene = new QGraphicsScene(this);
    setScene(scene);

    // Явно инициализируем сетки
    playerGrid.resize(gridSize, std::vector<Cell>(gridSize, Empty));
    opponentGrid.resize(gridSize, std::vector<Cell>(gridSize, Empty));

    // Добавьте эти проверки:
    qDebug() << "Constructor - scene created";
    qDebug() << "Default grid size:" << gridSize << "cell size:" << cellSize;

    setFixedSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    setBackgroundBrush(QBrush(Qt::black));

    setFocusPolicy(Qt::StrongFocus);
    setFocus();
    setMouseTracking(true);

    showGameOptions();
    hitSound.setSource(QUrl::fromLocalFile(":/sounds/hit.wav"));
    hitSound.setVolume(0.8f);

    missSound.setSource(QUrl::fromLocalFile(":/sounds/miss.wav"));
    missSound.setVolume(0.8f);

    winSound.setSource(QUrl("qrc:/sounds/win.wav"));
    winSound.setVolume(0.8f);
    loseSound.setSource(QUrl("qrc:/sounds/lose.wav"));
    loseSound.setVolume(0.8f);
}

void BattleShipGame::showGameOptions() {
    QDialog optionsDialog(this);
    optionsDialog.setWindowTitle("Настройки игры");

    QVBoxLayout *layout = new QVBoxLayout(&optionsDialog);

    // Выбор размера поля
    QGroupBox *sizeGroup = new QGroupBox("Размер поля", &optionsDialog);
    QVBoxLayout *sizeLayout = new QVBoxLayout;
    QRadioButton *size8 = new QRadioButton("8x8", sizeGroup);
    QRadioButton *size10 = new QRadioButton("10x10 (по умолчанию)", sizeGroup);
    QRadioButton *size12 = new QRadioButton("12x12", sizeGroup);
    size10->setChecked(true);

    sizeLayout->addWidget(size8);
    sizeLayout->addWidget(size10);
    sizeLayout->addWidget(size12);
    sizeGroup->setLayout(sizeLayout);

    // Режим мин
    QCheckBox *minesCheck = new QCheckBox("Режим 'Мины' (2 мины на поле)", &optionsDialog);

    // Кнопки
    QPushButton *okButton = new QPushButton("Начать игру", &optionsDialog);
    QPushButton *cancelButton = new QPushButton("Выход", &optionsDialog);

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);

    layout->addWidget(sizeGroup);
    layout->addWidget(minesCheck);
    layout->addLayout(buttonLayout);

    connect(okButton, &QPushButton::clicked, [&]() {
        if (size8->isChecked()) gridSize = Size8x8;
        else if (size10->isChecked()) gridSize = Size10x10;
        else gridSize = Size12x12;

        cellSize = (gridSize == Size12x12) ? 35 : DEFAULT_CELL_SIZE;
        minesEnabled = minesCheck->isChecked();

        optionsDialog.accept();
        qDebug() << "Options selected - gridSize:" << gridSize
                 << "cellSize:" << cellSize
                 << "minesEnabled:" << minesEnabled;

        initializeGame();
    });

    connect(cancelButton, &QPushButton::clicked, &optionsDialog, &QDialog::reject);

    if (optionsDialog.exec() == QDialog::Rejected) {
        QCoreApplication::quit();
        return;
    }
}

void BattleShipGame::initializeGame() {
    qDebug() << "Initializing game with gridSize:" << gridSize;

    // Проверка размера
    if (gridSize <= 0 || gridSize > Size12x12) {
        qCritical() << "Invalid grid size";
        return;
    }

    // Инициализация сеток
    playerGrid.clear();
    opponentGrid.clear();
    playerGrid.resize(gridSize, std::vector<Cell>(gridSize, Empty));
    opponentGrid.resize(gridSize, std::vector<Cell>(gridSize, Empty));

    initializeFleet();

    if (minesEnabled) {
        placeMines(playerGrid);
        placeMines(opponentGrid);
    }

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

void BattleShipGame::initializeFleet() {
    playerFleet.clear();
    opponentFleet.clear();

    switch (gridSize) {
    case Size8x8:
        playerFleet = {
            {3, 1, 1, "Линкор"},
            {2, 2, 2, "Крейсер"},
            {1, 3, 3, "Катер"}
        };
        break;
    case Size10x10:
        playerFleet = {
            {4, 1, 1, "Линкор"},
            {3, 2, 2, "Крейсер"},
            {2, 3, 3, "Эсминец"},
            {1, 4, 4, "Катер"}
        };
        break;
    case Size12x12:
        playerFleet = {
            {5, 1, 1, "Авианосец"},
            {4, 1, 1, "Линкор"},
            {3, 2, 2, "Крейсер"},
            {2, 3, 3, "Эсминец"},
            {1, 4, 4, "Катер"}
        };
        break;
    }

    opponentFleet = playerFleet;
}

void BattleShipGame::placeMines(Grid& grid) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, gridSize - 1);

    int placed = 0;
    while (placed < minesCount) {
        int x = dis(gen);
        int y = dis(gen);

        if (grid[x][y] == Empty) {
            grid[x][y] = Mine;
            placed++;
        }
    }
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
        QByteArray data = (message + "\n").toUtf8(); // Добавляем символ новой строки
        qint64 bytesWritten = socket->write(data);
        if (bytesWritten == -1) {
            qDebug() << "Failed to send message:" << socket->errorString();
        } else {
            qDebug() << "Message sent:" << message;
            socket->flush();
        }
    } else {
        qDebug() << "Cannot send message - no connection";
    }
}

void BattleShipGame::processCommand(const QString &command, const QString &data) {
    qDebug() << "Processing command:" << command << "data:" << data;

    if (command == "SHOT") {
        QStringList coords = data.split(',');
        if (coords.size() == 2) {
            int x = coords[0].toInt();
            int y = coords[1].toInt();

            if (playerGrid[x][y] == Ship) {
                playerGrid[x][y] = Hit;
                hitSound.play();

                // Проверяем, не потоплен ли корабль
                if (isShipSunk(playerGrid, x, y)) {
                    auto cells = getShipCells(playerGrid, x, y);
                    for (auto& s : playerFleet) {
                        if (s.remaining > 0 && s.size == (int)cells.size()) {
                            s.remaining--;
                            sendMessage("SUNK:" + QString::number(s.size));
                            showMessage("Противник потопил ваш " + s.name + "!", true);
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
            }
            else if (playerGrid[x][y] == Mine) {
                // Обработка попадания в мину
                hitSound.play();
                playerGrid[x][y] = Hit;

                // Взрыв мины - поражаем соседние клетки
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx;
                        int ny = y + dy;
                        if (isInside(nx, ny)) {
                            if (playerGrid[nx][ny] == Ship) {
                                playerGrid[nx][ny] = Hit;
                                if (isShipSunk(playerGrid, nx, ny)) {
                                    auto cells = getShipCells(playerGrid, nx, ny);
                                    for (auto& s : playerFleet) {
                                        if (s.remaining > 0 && s.size == (int)cells.size()) {
                                            s.remaining--;
                                            sendMessage("SUNK:" + QString::number(s.size));
                                            showMessage("Противник потопил ваш " + s.name + " (миной)!", true);
                                            break;
                                        }
                                    }
                                }
                            }
                            playerGrid[nx][ny] = Hit;
                        }
                    }
                }

                sendMessage("MINE_HIT:" + data);
                myTurn = true; // После мины ход остается у атаковавшего
            }
            else if (playerGrid[x][y] == Empty) {
                playerGrid[x][y] = Miss;
                missSound.play();
                sendMessage("MISS:" + data);
                myTurn = true; // Передаем ход обратно
                showMessage("Противник промахнулся! Ваш ход.", false);
            }

            drawGrids();
        }
    }
    else if (command == "HIT") {
        QStringList coords = data.split(',');
        if (coords.size() == 2) {
            int x = coords[0].toInt();
            int y = coords[1].toInt();
            opponentGrid[x][y] = Hit;
            hitSound.play();
            showMessage("Вы попали!", true);

            // Проверяем, не потоплен ли корабль
            if (isShipSunk(opponentGrid, x, y)) {
                auto cells = getShipCells(opponentGrid, x, y);
                for (auto& s : opponentFleet) {
                    if (s.remaining > 0 && s.size == (int)cells.size()) {
                        s.remaining--;
                        sendMessage("SUNK:" + QString::number(s.size));
                        showMessage("Вы потопили " + s.name + " противника!");
                        break;
                    }
                }
            }

            // Ход остаётся у текущего игрока при попадании
            myTurn = true;
        }
    }
    else if (command == "MISS") {
        QStringList coords = data.split(',');
        if (coords.size() == 2) {
            int x = coords[0].toInt();
            int y = coords[1].toInt();
            opponentGrid[x][y] = Miss;
            missSound.play();
            showMessage("Вы промахнулись!", true);
            myTurn = false; // Передаём ход противнику
        }
    }
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
    return x >= 0 && y >= 0 && x < gridSize && y < gridSize;
}

bool BattleShipGame::isSurroundingClear(Grid& grid, int x, int y, int size, bool horizontal) {
    for (int i = -1; i <= size; ++i) {
        for (int j = -1; j <= 1; ++j) {
            int nx = x + (horizontal ? i : j);
            int ny = y + (horizontal ? j : i);
            if (isInside(nx, ny) && grid[nx][ny] != Empty && grid[nx][ny] != Mine) // Игнорируем мины
                return false;
        }
    }
    return true;
}

bool BattleShipGame::canPlace(Grid& grid, int x, int y, int size, bool horizontal) {
    for (int i = 0; i < size; ++i) {
        int px = x + (horizontal ? i : 0);
        int py = y + (horizontal ? 0 : i);
        if (!isInside(px, py) || (grid[px][py] != Empty && grid[px][py] != Mine)) // Разрешаем ставить на мины
            return false;
    }
    return true; // Убрали проверку окружения, чтобы можно было ставить рядом с минами
}

void BattleShipGame::placeShip(Grid& grid, int x, int y, int size, bool horizontal) {
    for (int i = 0; i < size; ++i) {
        int px = x + (horizontal ? i : 0);
        int py = y + (horizontal ? 0 : i);
        grid[px][py] = Ship; // Если здесь была мина, она будет заменена на корабль
    }
}

std::vector<std::pair<int, int>> BattleShipGame::getShipCells(const Grid& grid, int x, int y) {
    std::vector<std::pair<int, int>> cells;
    if (grid[x][y] != Hit) return cells;

    int startX = x;
    while (startX > 0 && grid[startX - 1][y] == Hit) --startX;
    int endX = x;
    while (endX + 1 < gridSize && grid[endX + 1][y] == Hit) ++endX;
    if (endX - startX >= 1) {
        for (int i = startX; i <= endX; ++i) cells.emplace_back(i, y);
        return cells;
    }

    int startY = y;
    while (startY > 0 && grid[x][startY - 1] == Hit) --startY;
    int endY = y;
    while (endY + 1 < gridSize && grid[x][endY + 1] == Hit) ++endY;
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
    if (!scene) return;  // Защита от nullptr

    QFont font("Arial", 14, QFont::Bold);
    QFont smallFont("Arial", 10);

    // Заголовок поля
    QGraphicsTextItem *gridLabel = new QGraphicsTextItem(label);
    gridLabel->setFont(QFont("Arial", 16, QFont::Bold));
    gridLabel->setDefaultTextColor(label == "Игрок" ? COLOR_PLAYER_LABEL : COLOR_AI_LABEL);
    gridLabel->setPos(offsetX, offsetY - 70);
    scene->addItem(gridLabel);

    // Буквы сверху
    for (int x = 0; x < gridSize; ++x) {
        QGraphicsTextItem *letter = new QGraphicsTextItem(QString(QChar('A' + x)));
        letter->setFont(font);
        letter->setDefaultTextColor(COLOR_GRID_LABELS);
        letter->setPos(offsetX + x * cellSize + cellSize/2 - 7, offsetY - 40);
        scene->addItem(letter);
    }

    // Цифры слева
    for (int y = 0; y < gridSize; ++y) {
        QGraphicsTextItem *number = new QGraphicsTextItem(QString::number(y + 1));
        number->setFont(font);
        number->setDefaultTextColor(COLOR_GRID_LABELS);
        number->setPos(offsetX - 30, offsetY + y * cellSize + cellSize/2 - 10);
        scene->addItem(number);
    }

    // Рамка вокруг поля
    QGraphicsRectItem *border = new QGraphicsRectItem(
        offsetX - 3, offsetY - 3,
        gridSize * cellSize + 6, gridSize * cellSize + 6);
    QPen borderPen(label == "Игрок" ? COLOR_PLAYER_LABEL : COLOR_AI_LABEL, 3);
    borderPen.setJoinStyle(Qt::MiterJoin);
    border->setPen(borderPen);
    border->setBrush(Qt::NoBrush);
    scene->addItem(border);

    // Отрисовка клеток
    for (int x = 0; x < gridSize; ++x) {
        for (int y = 0; y < gridSize; ++y) {
            QGraphicsRectItem *cell = new QGraphicsRectItem(1, 1, cellSize - 4, cellSize - 4);
            cell->setPos(offsetX + x * cellSize + 1, offsetY + y * cellSize + 1);
            cell->setPen(Qt::NoPen);

            if (grid[x][y] == Ship && !showShips) {
                QLinearGradient grad(0, 0, 0, cellSize);
                grad.setColorAt(0, QColor(30, 60, 120));
                grad.setColorAt(1, QColor(10, 30, 80));
                cell->setBrush(grad);
            }
            else if (grid[x][y] == Ship) {
                cell->setBrush(QBrush(COLOR_SHIP));
            }
            else if (grid[x][y] == Hit) {
                QRadialGradient grad(cell->rect().center(), cellSize/2);
                grad.setColorAt(0, QColor(255, 80, 80));
                grad.setColorAt(1, QColor(180, 20, 20));
                cell->setBrush(grad);
            }
            else if (grid[x][y] == Miss) {
                cell->setBrush(QBrush(COLOR_MISS));
            }
            else if (grid[x][y] == Mine) {
                // Мины теперь вообще не рисуем - они полностью невидимы
                QLinearGradient grad(0, 0, 0, cellSize);
                grad.setColorAt(0, QColor(30, 60, 120));
                grad.setColorAt(1, QColor(10, 30, 80));
                cell->setBrush(grad);
            }
            else {
                QLinearGradient grad(0, 0, 0, cellSize);
                grad.setColorAt(0, QColor(30, 60, 120));
                grad.setColorAt(1, QColor(10, 30, 80));
                cell->setBrush(grad);
            }
            scene->addItem(cell);
        }
    }

    // Отрисовка информации о флоте
    QGraphicsRectItem *fleetBg = new QGraphicsRectItem(
        offsetX - 10, offsetY + gridSize * cellSize + 15,
        gridSize * cellSize + 20, fleetInfo.size() * 25 + 30);
    fleetBg->setBrush(QBrush(QColor(20, 40, 80, 200)));
    fleetBg->setPen(QPen(label == "Игрок" ? COLOR_PLAYER_LABEL : COLOR_AI_LABEL, 2));
    scene->addItem(fleetBg);

    for (size_t i = 0; i < fleetInfo.size(); ++i) {
        QGraphicsRectItem *shipIcon = new QGraphicsRectItem(
            offsetX, offsetY + gridSize * cellSize + 30 + i * 25,
            15, 15);
        shipIcon->setBrush(fleetInfo[i].remaining == 0 ? QBrush(Qt::gray) :
                               (label == "Игрок" ? QBrush(COLOR_PLAYER_LABEL) : QBrush(COLOR_AI_LABEL)));
        scene->addItem(shipIcon);

        QGraphicsTextItem *text = new QGraphicsTextItem(
            QString("  %1: %2/%3").arg(fleetInfo[i].name)
                .arg(fleetInfo[i].remaining)
                .arg(fleetInfo[i].count));
        text->setFont(smallFont);
        text->setPos(offsetX + 20, offsetY + gridSize * cellSize + 25 + i * 25);
        text->setDefaultTextColor(fleetInfo[i].remaining == 0 ? Qt::gray : COLOR_SHIP_COUNT);
        scene->addItem(text);
    }
}
void BattleShipGame::drawGrids() {
    if (!scene) return;  // Защита от nullptr

    scene->clear();

    // Рассчитываем позиции динамически
    int gridWidth = gridSize * cellSize;
    int spacing = 50;

    // Рисуем поле игрока (левое)
    drawGrid(spacing, spacing, playerGrid, playerFleet, true, "Игрок");

    // Рисуем поле противника (правое)
    drawGrid(spacing * 2 + gridWidth, spacing, opponentGrid, opponentFleet, false, "Противник");

    if (placing && currentShipIndex < playerFleet.size()) {
        QPoint mousePos = mapFromGlobal(QCursor::pos());
        QPointF pos = mapToScene(mousePos);
        int mx = (pos.x() - 50) / cellSize;
        int my = (pos.y() - 50) / cellSize;

        if (mx >= 0 && mx < gridSize && my >= 0 && my < gridSize) {
            auto& ship = playerFleet[currentShipIndex];
            if (ship.count > 0) {
                bool canPlaceHere = canPlace(playerGrid, mx, my, ship.size, horizontal);
                QColor previewColor = canPlaceHere ? Qt::yellow : Qt::red;

                // Рисуем контур корабля
                for (int i = 0; i < ship.size; ++i) {
                    int px = mx + (horizontal ? i : 0);
                    int py = my + (horizontal ? 0 : i);
                    if (px < gridSize && py < gridSize) {
                        QGraphicsRectItem *outline = new QGraphicsRectItem(0, 0, cellSize- 2, cellSize- 2);
                        outline->setPos(50 + px * cellSize, 50 + py * cellSize);
                        outline->setBrush(Qt::NoBrush);
                        outline->setPen(QPen(previewColor, 2));
                        scene->addItem(outline);
                    }
                }

                // Подпись с названием и размером корабля
                QGraphicsTextItem *shipInfo = new QGraphicsTextItem(ship.name + " (" + QString::number(ship.size) + " клетки)");
                shipInfo->setFont(QFont("Arial", 12));
                shipInfo->setDefaultTextColor(Qt::white);
                shipInfo->setPos(50, 6);
                scene->addItem(shipInfo);
            }
        }
    }

    // Сообщение о состоянии игры
    if (!currentMessage.isEmpty()) {
        QGraphicsTextItem *msg = new QGraphicsTextItem(currentMessage);
        msg->setFont(QFont("Arial", 16, QFont::Bold));
        msg->setDefaultTextColor(COLOR_WAITING);
        QRectF rect = msg->boundingRect();
        msg->setPos(width()/2 - rect.width()/2, height() - 50);
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
        winSound.play();
    } else {
        showMessage("К сожалению, вы проиграли...", false);
        loseSound.play();
    }
}

void BattleShipGame::mousePressEvent(QMouseEvent *event) {
    if (gameEnded) return;

    if (placing) {
        if (event->button() == Qt::LeftButton && currentShipIndex < playerFleet.size()) {
            QPointF pos = mapToScene(event->pos());
            int mx = (pos.x() - 50) / cellSize;
            int my = (pos.y() - 50) / cellSize;

            if (mx >= 0 && mx < gridSize && my >= 0 && my < gridSize) {
                auto& ship = playerFleet[currentShipIndex];
                if (ship.count > 0 && canPlace(playerGrid, mx, my, ship.size, horizontal)) {
                    // При размещении корабля заменяем мину на корабль
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
            int playerGridWidth = gridSize * cellSize;
            int opponentGridStartX = 50 + playerGridWidth + 50;
            int mx = (pos.x() - opponentGridStartX) / cellSize;
            int my = (pos.y() - 50) / cellSize;

            if (mx >= 0 && mx < gridSize && my >= 0 && my < gridSize) {
                // Проверяем, что по этой клетке ещё не стреляли
                if (opponentGrid[mx][my] == Empty || opponentGrid[mx][my] == Mine) {
                    if (socket && socket->state() == QAbstractSocket::ConnectedState) {
                        // Запоминаем координаты выстрела
                        lastShotX = mx;
                        lastShotY = my;

                        if (opponentGrid[mx][my] == Mine) {
                            // Обработка попадания в мину
                            hitSound.play();
                            sendMessage("MINE:" + QString::number(mx) + "," + QString::number(my));
                        } else {
                            // Обычный выстрел
                            sendMessage("SHOT:" + QString::number(mx) + "," + QString::number(my));
                        }

                        // Не меняем ход здесь - дождёмся ответа от противника
                        showMessage("Ожидаем ответ противника...", false);
                        drawGrids();
                    } else {
                        showMessage("Нет подключения к противнику!", true);
                    }
                } else {
                    showMessage("Вы уже стреляли в эту клетку!", true);
                }
            } else {
                showMessage("Выстрел за пределы поля!", true);
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
