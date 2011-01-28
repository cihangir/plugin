#include "donothing.h"

#include <QtPlugin>
#include <QStringList>
#include <QMessageBox>
#include <qdebug.h>

#include <coreplugin/coreconstants.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/icore.h>

#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>

#include <QUiLoader>
#include <QSettings>
#include <QXmlStreamReader>
#include <QDir>
#include "settingsdialog.h"

DoNothingPlugin::DoNothingPlugin() :
        mime_type("application/x-designer"),
        connected(false),
        portNumber(33666)
{
    // Do nothing
    fm = Core::ICore::instance()->fileManager();
    timer.start();
}

DoNothingPlugin::~DoNothingPlugin()
{
    // Do notning
}

bool DoNothingPlugin::initialize(const QStringList& args, QString *errMsg)
{
    Q_UNUSED(args);
    Q_UNUSED(errMsg);

    createMenuItems();
    connect(fm, SIGNAL(currentFileChanged(QString)), this, SLOT(changeWatchedFile(QString)));
    connect(&watcher, SIGNAL(fileChanged(QString)), this, SLOT(handleFileChange(QString)));

    connect(&socket, SIGNAL(connected()), this, SLOT(showConnected()));
    connect(&socket, SIGNAL(disconnected()), this, SLOT(disconnectedSlot()));
    connect(&socket, SIGNAL(readyRead()), this, SLOT(readMessage()));

    QSettings set("Bilkon", "DoNothing");
    QString ipAddress = set.value("ipAddress").toString();

    if (!ipAddress.isEmpty() && portNumber != 0)
        socket.connectToHost(ipAddress, portNumber);

    return true;
}

void DoNothingPlugin::extensionsInitialized()
{
    // Do nothing
}

void DoNothingPlugin::changeWatchedFile(QString fileName)
{
    if (!oldFileName.isEmpty())
        watcher.removePath(oldFileName);

    if (!watcher.files().contains(fileName))
        watcher.addPath(fileName);
    oldFileName = fileName;
}

void DoNothingPlugin::readMessage()
{
    qDebug() << "DoNothingPlugin::readMessage";

    again:

    int messageType;
    QDataStream in(&socket);
    in.setVersion(QDataStream::Qt_4_6);

    if (blocksize == 0) {
        if (socket.bytesAvailable() < sizeof(quint64))
            return;

        in >> blocksize;
    }

    if (socket.bytesAvailable() < blocksize)
        return;

    in >> messageType;

    QString fileName, errorString;

    qDebug() << "messageType" << messageType;

    switch (messageType) {
    case DoNothingPlugin::FILE:
        ba.clear();
        in >> fileName;
        in >> ba;
        //writeToFile(fileName, ba);
        break;
    case DoNothingPlugin::MAP:
        break;
    case DoNothingPlugin::COMMAND:
        in >> errorString;
        //processErrorMessage(errorString);
        break;
    case DoNothingPlugin::FILE_LIST:
        qDebug() << "File list received from server";
        filesOnServer.clear();
        in >> filesOnServer;
        qDebug() << "List" << filesOnServer;
        break;
    default:
        qDebug() << "Unknown data format!";
    }

    blocksize = 0;

    qDebug() << "Available bytes" << socket.bytesAvailable();
    if (socket.bytesAvailable())
        goto again;
}

void DoNothingPlugin::shutdown()
{
    // Do nothing
    socket.disconnectFromHost();
}

void DoNothingPlugin::about()
{
    QMessageBox::information(reinterpret_cast<QWidget *>(Core::ICore::instance()->mainWindow()),
                             "About Bilkon Plugin",
                             "Bilkon UI Designer Plugin");
}

void DoNothingPlugin::printModifiedFiles()
{
    QList<Core::IFile *> list = modifiedFiles();
    qDebug() << "size of list:" << list.size();
    foreach (Core::IFile *file, list) {
        qDebug() << file->fileName() << file->mimeType();
    }
}

void DoNothingPlugin::handleFileChange(const QString & path)
{
    static bool firstTime = true;

    if (timer.elapsed() < 150)
        return;
    timer.restart();
    qDebug() << "Following file changed" << path;

    QWidget *widget = load(path);

    if (!checkNames(widget)) {
        QMessageBox::warning(reinterpret_cast<QWidget *>(Core::ICore::instance()->mainWindow()),
                             "Error",
                             "Check widget names!");
        return;
    }

    QFile file(path);
    file.open(QFile::ReadOnly);
    QByteArray array = file.readAll();

    qDebug() << "File size" << array.size();

    sendMessage("foobar.ui", array);

    if (firstTime) {
        qDebug() << "First time of my life";
        QFileInfo fileInfo(path);
        sendImages(fileInfo.absolutePath());
        connect(&imageWatcher, SIGNAL(fileChanged(QString)), this, SLOT(insertFile(QString)));

        QDir dir(fileInfo.absolutePath());
        foreach (QFileInfo fileName, dir.entryInfoList()) {
            if (fileName.suffix() != "ui" &&
                fileName.fileName() != ".." &&
                fileName.fileName() != ".") {
                imageWatcher.addPath(fileName.absoluteFilePath());
                qDebug() << "imagewatcher" << fileName.fileName();
            }
        }

        firstTime = false;
    } else {
        foreach (QString fileName, imagesToSend) {
            QFileInfo fileInfo(fileName);

            if (fileInfo.exists()) {
                QFile imFile(fileInfo.absoluteFilePath());
                imFile.open(QFile::ReadOnly);
                array = imFile.readAll();

                sendMessage(fileInfo.fileName(), array);
            }
        }

        imagesToSend.clear();
    }

    delete widget;
}

void DoNothingPlugin::insertFile(const QString & path)
{
    if (!imagesToSend.contains(path) ||
        path.split("/").last() != "." ||
        path.split("..").last() != ".." ||
        path.split(".").last() != "ui") {
        imagesToSend.append(path);
        qDebug() << "inserted file" << path;
    }
}

void DoNothingPlugin::settings()
{
    QSettings set("Bilkon", "DoNothing");
    settingsDialog dialog(reinterpret_cast<QWidget *>(Core::ICore::instance()->mainWindow()));
    dialog.setIpAddress(set.value("ipAddress").toString());
    qDebug() << "Connected:" << connected;
    dialog.setStatus(connected ? "Connected" : "Disconnected");

    if (dialog.exec()) {
        QString ipAddress = dialog.ipAddress();

        if (!ipAddress.isEmpty()) {
            set.setValue("ipAddress", ipAddress);

            if (!connected)
                socket.connectToHost(ipAddress, portNumber);
        }
    }
}

void DoNothingPlugin::showConnected()
{
    qDebug() << "Connected";
    connected = true;
}

void DoNothingPlugin::disconnectedSlot()
{
    qDebug() << "Disconnected";
    connected = false;
}

void DoNothingPlugin::createMenuItems()
{
    Core::ActionManager* am = Core::ICore::instance()->actionManager();
    Core::ActionContainer *ac = am->createMenu("BilkonPlugin.BilkonMenu");
    ac->menu()->setTitle("Bilko&n");

    Core::Command *cmd = am->registerAction(new QAction(this),
                                            "DoNothingPlugin.DoNothingMenu",
                                            QList<int>() << 0);
    cmd->action()->setText("&About Bilkon Plugin");
    am->actionContainer(Core::Constants::MENU_BAR)->addMenu(ac);
    ac->addAction(cmd);
    connect(cmd->action(), SIGNAL(triggered(bool)), this, SLOT(about()));

    QAction *settingsAction = ac->menu()->addAction("&Settings");
    connect(settingsAction, SIGNAL(triggered(bool)), this, SLOT(settings()));
}

bool DoNothingPlugin::isValid(const QString & objName) const
{
    return true;

    QStringList list = objName.split("_");
    if (list.size() >= 1)
        return false;

    if (classMap.contains(list[0])) {
        if (!classMap.value(list[0]).contains(list[1]))
            return false;
    }

    return true;
}

QList<Core::IFile *> DoNothingPlugin::modifiedFiles() const
{
    QList<Core::IFile *> list;
    //fm = Core::ICore::instance()->fileManager();

    foreach (Core::IFile *uiFile, fm->modifiedFiles()) {
        if (uiFile->mimeType() == mime_type)
            list.append(uiFile);
    }

    return list;
}

QWidget* DoNothingPlugin::load(const QString & fileName)
{
    QUiLoader loader;
    QFile file(fileName);
    file.open(QFile::ReadOnly);

    return loader.load(&file, NULL);
}

bool DoNothingPlugin::checkNames(const QWidget *widget) const
{

    if (widget == NULL)
        return false;

    qDebug() << "checkNames" << widget;
    QList<QWidget *> list = widget->findChildren<QWidget *>();
    qDebug() << "checkNames" << list.size() << list.first()->objectName();

    foreach (QWidget *w, list) {
        qDebug() << w->objectName();
        if (!isValid(w->objectName()))
            return false;
    }

    return true;
}

QStringList DoNothingPlugin::parseResource(const QString &fileName) const
{
    QStringList resourceMap;
    QFile file(fileName);
    if(!file.open(QFile::ReadOnly))
        return resourceMap;


    QByteArray ba = file.readAll();
    qDebug() << ba;
    QXmlStreamReader xmlReader(ba);
    qDebug() << "DoNothingPlugin::parseResource" << fileName;
    while (!xmlReader.atEnd()) {
        QXmlStreamReader::TokenType type = xmlReader.readNext();
        if (type == QXmlStreamReader::StartElement) {
            qDebug() << xmlReader.readElementText() << xmlReader.name();
        }
    }

    if (xmlReader.hasError())
        qDebug() << "An error occurred" << xmlReader.errorString();

    return resourceMap;
}

void DoNothingPlugin::sendMessage(const QString & fileName, const QByteArray & data)
{
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_4_6);

    out << (quint32) 0;
    out << (quint32) DoNothingPlugin::FILE;
    out << fileName;
    out << data;

    out.device()->seek(0);
    out << (quint32) (block.size() - sizeof(quint32));

    socket.write(block);
}

void DoNothingPlugin::sendMessage(const QString & string)
{
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_4_6);

    out << (quint32) 0;
    out << (quint32) DoNothingPlugin::COMMAND;
    out << string;

    out.device()->seek(0);
    out << (quint32) (block.size() - sizeof(quint32));

    socket.write(block);
}

void DoNothingPlugin::sendMessage(const QMap<QString, QStringList> & classMap)
{
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_4_6);

    out << (quint32) 0;
    out << (quint32) DoNothingPlugin::MAP;
    out << classMap;

    out.device()->seek(0);
    out << (quint32) (block.size() - sizeof(quint32));

    socket.write(block);
}

void DoNothingPlugin::sendImages(const QString & path)
{
    QDir dir(path);
    qDebug() << "Current dir:" << path;

    QByteArray array;
    QFileInfoList list = dir.entryInfoList();

    foreach (QFileInfo fileInfo, list) {
        if (fileInfo.suffix().compare("png", Qt::CaseInsensitive) == 0 ||
            fileInfo.suffix().compare("jpg", Qt::CaseInsensitive) == 0 ||
            fileInfo.suffix().compare("jpeg", Qt::CaseInsensitive) == 0) {

            qDebug() << "Filename:" << fileInfo.fileName();

            QFile file(fileInfo.absoluteFilePath());
            if (!file.open(QFile::ReadOnly))
                continue;

            array = file.readAll();
            qDebug() << "imagesize =" << array.size();
            sendMessage(fileInfo.fileName(), array);
        }
    }
}

void print_trace (void)
{
    void *array[10];
    size_t size;
    char **strings;
    size_t i;

    size = backtrace (array, 10);
    strings = backtrace_symbols (array, size);

    printf ("Obtained %zd stack frames.\n", size);

    for (i = 0; i < size; i++)
        printf ("%s\n", strings[i]);

    free (strings);
}

Q_EXPORT_PLUGIN(DoNothingPlugin)
