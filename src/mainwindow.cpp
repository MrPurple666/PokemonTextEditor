#include "mainwindow.h"
#include "searchwindow.h"
#include "insertwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <algorithm>
#include <string.hpp>


bool blockActive = false;


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupui(false);
    loadtable();
    ui->treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(on_treeWidget_customContextMenuRequested(QPoint)));
}

MainWindow::~MainWindow()
{
    delete ui;
}


const char *MainWindow::readByteArray(int n)
{
    char *buffer = new char[n];
    rom.read(buffer, (uint)n);
    return buffer;
}

unsigned int MainWindow::readWord()
{
    char buffer[4];
    rom.read(buffer, 4U);
    return (uint)((buffer[3] << 24) | (buffer[2] << 16) | (buffer[1] << 8) | buffer[0]);
}

unsigned short MainWindow::readHWord()
{
    char buffer[2];
    rom.read(buffer, 2U);
    return (uint)((buffer[1] << 8) | buffer[0]);
}

unsigned char MainWindow::readByte()
{
    char buffer;
    rom.read(&buffer, 1);
    return buffer;
}

void MainWindow::seek(uint offs)
{
    bypos = offs;
}

void MainWindow::writeByteArray(const QByteArray &array)
{
    if (byrom + bypos + array.size() >= byrom + szrom)
    {
        QMessageBox::warning(this, "WriteByteArray", "Warning: Tried to write outside ROM! Aborted.");
        return;
    }

    memcpy(byrom + bypos, array.data(), array.size());
}

void MainWindow::writeWord(unsigned int word)
{
    if (byrom + bypos + 4 >= byrom + szrom)
    {
        QMessageBox::warning(this, "WriteWord", "Warning: Tried to write outside ROM! Aborted.");
        return;
    }

    (*(uint*)(byrom + bypos)) = word;
}

void MainWindow::writeHWord(unsigned short hword)
{
    if (byrom + bypos + 2 >= byrom + szrom)
    {
        QMessageBox::warning(this, "WriteHWord", "Warning: Tried to write outside ROM! Aborted.");
        return;
    }

    (*(ushort*)(byrom + bypos)) = hword;
}

void MainWindow::writeByte(unsigned char byte)
{
    if (byrom + bypos + 1 >= byrom + szrom)
    {
        if (byrom + bypos + 4 >= byrom + szrom)
        {
            QMessageBox::warning(this, "WriteByte", "Warning: Tried to write outside ROM! Aborted.");
            return;
        }
    }


    *(byrom + bypos) = byte;
}


void MainWindow::repoint(uint oldoff, uint newoff)
{
    // make pointer of 'oldoff'
    uint ptr = oldoff + 0x08000000;
    char *bptr = new char[4];
    ((uint*)(bptr))[0] = ptr;

    // make pointer of 'newoff'
    uint nptr = newoff + 0x08000000;
    char *bnptr = new char[4];
    ((uint*)(bnptr))[0] = nptr;

    // search for references to 'oldoff' and replace them
    // with the new pointer to 'newoff'.
    auto it = std::search(byrom, byrom + szrom, bptr, bptr + 4);
    if (it == (byrom + szrom))
    {
        QMessageBox::information(this, "Repoint", "Pointer to text not found! Repointing aborted.");
        return;
    }
    else
    {
        // find and replace all
        QStringList foundoffs;
        uchar *current = byrom;
        uint remSize = szrom - (byrom - current);
        do
        {
            uint off = std::distance(byrom, it);
            foundoffs.push_back(QString::number(off, 16));
            seek(off);
            writeWord(nptr);
            current = byrom + off;
            remSize = szrom - (byrom - current);
            it = std::search(current, current + remSize, bptr, bptr + 4);
        } while (it != (byrom + szrom));

        // generate message string
        QString msg;
        msg.append("References replaced. Found at:\n\n");
        foreach (QString s, foundoffs)
            msg.append(QString("    0x") + s + "\n");
        QMessageBox::information(this, "Repoint", msg);
    }
}

void MainWindow::setupui(bool b)
{
    ui->actionSave_ROM->setEnabled(b);
    ui->actionSearch_Text->setEnabled(b);
    ui->actionCreate_specific_INI->setEnabled(b);
    ui->actionWrite_current_text->setEnabled(b);
    ui->treeWidget->setEnabled(b);
    ui->plainTextEdit->setEnabled(b);

    if (!b)
    {
        ui->treeWidget->clear();
        ui->statusbar->clearMessage();
    }
    else
    {
        // create backup
        QString fpbak(fprom);
        fpbak.replace(".gba", ".pte_bak");
        QFile bakfile(fpbak);
        bakfile.open(QIODevice::ReadWrite);
        bakfile.flush();
        QFile romfile(fprom);
        romfile.copy(fpbak);
        szrom = romfile.size();
        romfile.close();

        // loads all the rom bytes
        byrom = new u_int8_t[szrom];
        rom.read((char*)byrom, szrom);

        // load ini etc
        QFileInfo info(fprom);
        ui->statusbar->showMessage(QString("ROM: " + info.fileName()));
        loadini();
    }

}

int getIniOffset(QString &str, int line)
{
    int base = 10;
    bool success = false;
    if (str.startsWith("0x", Qt::CaseInsensitive) |
        str.startsWith("&H", Qt::CaseInsensitive))
    {
        str.remove(0, 2);
        base = 16;
    }
    else if (str.startsWith("$", Qt::CaseInsensitive))
    {
        str.remove(0, 1);
        base = 16;
    }


    int num = str.toInt(&success, base);
    if (success)
        return num;

    QMessageBox::warning(qApp->activeWindow(), "INI", QString("Invalid offset at line %0. Expected decimal or hexadecimal input with prefixes '0x', '&H' or '$'.").arg(QString::number(line)));
    return 0;
}

void MainWindow::loadini()
{
    ui->treeWidget->setUpdatesEnabled(false);

    // seek to header
    rom.seekg(0xAC);

    // if there is specific ini, load it instead
    QFile file;
    QFileInfo firom(fprom);
    QString phrom = firom.absolutePath() + "/";
    QString fpspe = phrom + firom.baseName() + "_PTE.ini";
    file.setFileName(fpspe);
    if (!file.exists())
    {
        // check if ini exists
        QString app = QApplication::applicationDirPath();
        QString fld = QString("/ini/");
        QString fle = QString(QByteArray(readByteArray(4)));
        QString ini = app + fld + fle + ".ini";
        file.setFileName(ini);
        fpini = ini;

        if (!file.exists())
        {
            QMessageBox::information(this, "Warning", "INI file not found for this ROM type. Created a new one.");
            file.open(QIODevice::ReadWrite);
            file.flush();
        }
        else
        {
            file.open(QIODevice::ReadOnly);
        }
    }
    else
    {
        fpini = fpspe;
        file.open(QIODevice::ReadOnly);
    }

    // reads the ini file
    QString initext = file.readAll().toStdString().c_str();
    QStringList inilines = initext.split("\n", QString::SkipEmptyParts);
    QTreeWidgetItem *lastItem;

    int currentTopPos = -1;
    int lastLevel = -1;
    for (int i = 0; i < inilines.size(); i++) // where i=linenumber
    {
        QString line = inilines.at(i).trimmed();
        int32_t level = -1;
        for (int j = 0; j < line.length(); j++, level++)
            if (line.at(j).toLatin1() != '-')
                break;

        if (level == -1 && !line.contains('='))
        {
            // invalid item
            if (QMessageBox::warning(this, "INI", QString("Invalid specifier in line %0. Click 'yes' to continue with next line or 'no' to abort."), QMessageBox::Yes, QMessageBox::No) == QMessageBox::No)
                return;
            else
                continue;
        }
        if (level == 0)
        {
            QTreeWidgetItem *top = new QTreeWidgetItem;
            top->setText(0, line.mid(1));
            top->setData(0, Qt::UserRole, QVariant(0));
            currentTopPos++;
            lastItem = top;
            ui->treeWidget->addTopLevelItem(top);
        }
        else if (level != -1)
        {
            // retrieves parental item
            QTreeWidgetItem *parent = ui->treeWidget->topLevelItem(currentTopPos);
            int parentlevel = level - 1;
            while (parentlevel > 0)
            {
                parent = parent->child(parent->data(0, Qt::UserRole).toInt());
                parentlevel--;
            }

            // adds new sub-item
            QTreeWidgetItem *sub = new QTreeWidgetItem;
            sub->setText(0, line.mid(1 + level));
            sub->setData(0, Qt::UserRole, QVariant(0));
            parent->addChild(sub);
            lastItem = sub;

            if (lastLevel == level)
            {
                parent->setData(0, Qt::UserRole, parent->data(0, Qt::UserRole).toInt() + 1);
            }
        }
        else
        {
            // is a text entry
            QStringList entrypair = line.split('=');
            QTreeWidgetItem *sub = new QTreeWidgetItem;
            sub->setText(0, entrypair.at(0));
            sub->setData(0, Qt::UserRole, getIniOffset(entrypair[1], i));
            lastItem->addChild(sub);
        }


        lastLevel = level;
    }

    ui->treeWidget->setUpdatesEnabled(true);
    ui->treeWidget->update();
}

int reclevel = 0;
void parseAllChildren(QTreeWidgetItem *parent, QString &output)
{
    // add indent
    for (int i = 0; i < reclevel*4; i++)
        output.append(' ');

    // add level and text
    for (int i = 0; i <= reclevel; i++)
        output.append('-');

    output.append(parent->text(0));
    output.append('\n');

    // parses all children
    for (int i = 0; i < parent->childCount(); i++)
    {
        QTreeWidgetItem *child = parent->child(i);
        if (child->data(0, Qt::UserRole).toUInt() == 0) // is folder
        {
            reclevel++;
            parseAllChildren(child, output);
            reclevel--;
        }
        else // is entry
        {
            // add indent
            for (int j = 0; j < (reclevel+1)*4; j++)
                output.append(' ');

            // add text and offset
            output.append(child->text(0));
            output.append("=0x");
            output.append(QString::number(child->data(0, Qt::UserRole).toUInt(), 16));
            output.append('\n');
        }
    }
}

void MainWindow::writeini()
{
    reclevel = 0;

    QString output = "";
    QTreeWidgetItem *master = ui->treeWidget->topLevelItem(0);
    parseAllChildren(master, output);

    QFile fini(fpini);
    fini.open(QIODevice::WriteOnly);
    fini.write(output.toUtf8());
    fini.flush();
    fini.close();
}

void MainWindow::on_actionOpen_ROM_triggered()
{
    QString file = QFileDialog::getOpenFileName(
                this,
                "Open ROM file",
                QDir::homePath(),
                "ROMs (*.gba)"
    );

    if (file.isEmpty())
        return;

    fprom = file;
    if (rom.is_open())
    {
        rom.close();
        setupui(false);
        delete byrom;
    }

    rom.open(file.toStdString(), std::ios_base::in|std::ios_base::out|std::ios_base::binary);
    if (!rom.is_open())
        QMessageBox::information(this, "Error", "ROM is already in use!");
    else
        setupui(true);
}

void MainWindow::on_actionExit_triggered()
{
    qApp->closeAllWindows();
    qApp->exit();
}

void MainWindow::on_treeWidget_itemDoubleClicked(QTreeWidgetItem *item, int column)
{
    // other window owns treeview temporarily
    if (blockActive)
        return;

    // must be colum zero and a text entry
    if (column != 0)
        return;

    if (item->data(0, Qt::UserRole).toUInt() == 0)
        return;

    // loads text and displays it
    uint textlen = 0;
    uint offs = item->data(0, Qt::UserRole).toUInt();
    QString text = readpokestring(byrom, offs, false, &textlen);
    item->setData(1, Qt::UserRole, QVariant(textlen));

    // inserts some readable whitespace
    text.replace("\\p", "\\p\n");
    text.replace("\\l", "\\l\n");
    text.replace("\\n", "\\n\n");

    ui->plainTextEdit->setPlainText(text);
    ui->statusbar2->showMessage(QString("Offset: 0x%0, Length: %1 bytes").arg(QString::number(offs, 16), QString::number(textlen)));
}

void MainWindow::on_actionSave_ROM_triggered()
{
    QFile from(fprom);
    from.open(QIODevice::WriteOnly);
    from.write((char*)byrom, szrom);
    from.flush();
    from.close();

    QMessageBox::information(this, "ROM", "ROM saved successfully.");
}

void MainWindow::on_actionCreate_specific_INI_triggered()
{
    if (QMessageBox::question(
                this,
                "INI",
                "A specific INI is an INI that is stored inside your ROM folder and therefore belongs exclusively to your ROM. Do you want to create one?"
    ) == QMessageBox::Yes)
    {
        QFileInfo firom(fprom);
        QString phrom = firom.absolutePath() + "/";
        QString fpspe = phrom + firom.baseName() + "_PTE.ini";

        if (fpspe == fpini)
        {
            QMessageBox::information(this, "INI", "Specific INI already created!");
            return;
        }

        QFile fini(fpini);
        fini.open(QIODevice::ReadOnly);
        fini.copy(fpspe);
        fpini = fpspe;

        QMessageBox::information(
                    this,
                    "INI",
                    "Specific INI created. Every action will now be operated on this INI."
        );
    }
}

void MainWindow::on_actionSearch_Text_triggered()
{
    searchwindow sw;
    sw.setrom(byrom, szrom);
    sw.exec();

    if (sw.result() == 1)
    {
        // choose ini entry
        insertwindow iw;
        QTreeWidget *w = ui->treeWidget;
        qint32 max = w->maximumWidth();

        centralWidget()->setUpdatesEnabled(false);
        disconnect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(on_treeWidget_customContextMenuRequested(QPoint)));
        ui->treeWidget->setMaximumWidth(16777215);
        iw.settree(ui->treeWidget);
        blockActive = true;
        iw.exec();
        blockActive = false;
        ui->gridLayout->addWidget(w, 0, 0, 2, 1);
        connect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(on_treeWidget_customContextMenuRequested(QPoint)));
        centralWidget()->setUpdatesEnabled(true);
        w->setMaximumWidth(max);

        if (iw.result() == 1)
        {
            // add ini entry
            QTreeWidgetItem *folder = iw.selected();
            QTreeWidgetItem *child = iw.inserted();
            child->setData(0, Qt::UserRole, QVariant(sw.selectedOff()));
            folder->addChild(child);
            writeini();
        }
    }
}

bool canRequest = true;
void MainWindow::on_treeWidget_customContextMenuRequested(const QPoint &pos)
{
    QTreeWidgetItem *item = ui->treeWidget->itemAt(pos);
    if (canRequest)
    {
        canRequest = false;
    }
    else
    {
        canRequest = true;
        return;
    }

    if (item->data(0, Qt::UserRole).toUInt() == 0)
    {
        // is folder
        QMenu contextMenu;
        QAction *addSub = contextMenu.addAction("Add sub-folder");
        QAction *addTxt = contextMenu.addAction("Add text entry");
        QAction *remSub = contextMenu.addAction("Remove sub-folder");
        QAction *rename = contextMenu.addAction("Rename sub-folder");
        QAction *result = contextMenu.exec(ui->treeWidget->mapToGlobal(pos));

        if (result == addSub)
        {
            bool result = false;
            QString name = QInputDialog::getText(
                        this,
                        "Choose folder name",
                        "Name:",
                        QLineEdit::Normal,
                        "Folder",
                        &result);

            if (result && !name.isEmpty())
            {
                QTreeWidgetItem *i = new QTreeWidgetItem;
                i->setText(0, name);
                i->setData(0, Qt::UserRole, 0U);
                item->addChild(i);
                writeini();
                ui->treeWidget->update();
            }
        }
        else if (result == addTxt)
        {
            bool result = false;
            QString name = QInputDialog::getText(
                        this,
                        "Choose entry name",
                        "Name:",
                        QLineEdit::Normal,
                        "Entry",
                        &result);

            if (!result || name.isEmpty())
                return;

            uint acof;
            QString offs = QInputDialog::getText(
                        this,
                        "Choose offset (decimal or hex)",
                        "Offset (Prefixes 0x, &&H and $ accepted):",
                        QLineEdit::Normal,
                        "0x0",
                        &result);

            bool conv;
            if (offs.startsWith("0x", Qt::CaseInsensitive) ||
                offs.startsWith("&H", Qt::CaseInsensitive))
            {
                offs.remove(0, 2);
                acof = offs.toUInt(&conv, 16);
            }
            else if (offs.startsWith("$"))
            {
                offs.remove(0, 1);
                acof = offs.toUInt(&conv, 16);
            }
            else
                acof = offs.toUInt(&conv, 10);

            if (conv && (acof == 0 || acof >= szrom))
            {
                QMessageBox::warning(
                            this,
                            "Add entry",
                            "Given offset is either zero or is out of the ROM.");

                return;
            }
            if (result && conv)
            {
                QTreeWidgetItem *i = new QTreeWidgetItem;
                i->setText(0, name);
                i->setData(0, Qt::UserRole, acof);
                item->addChild(i);
                writeini();
                ui->treeWidget->update();
            }
        }
        else if (result == remSub)
        {
            QString msg("The folder and all it's sub-items will be removed. Do you really want to remove this folder?");
            QMessageBox::StandardButton result = QMessageBox::question(this, "Folder", msg);

            if (result == QMessageBox::Yes)
            {
                item->parent()->removeChild(item);
                writeini();
                ui->treeWidget->update();
            }
        }
        else if (result == rename)
        {
            bool result = false;
            QString name = QInputDialog::getText(
                        this,
                        "Choose new name",
                        "Name:",
                        QLineEdit::Normal,
                        item->text(0),
                        &result);

            if (result && !name.isEmpty())
            {
                item->setText(0, name);
                writeini();
                ui->treeWidget->update();
            }
        }
    }
    else
    {
        // is text entry
    }
}
