/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * Copyright (C) 2013 Open Education Foundation
 *
 * Copyright (C) 2010-2013 Groupement d'Intérêt Public pour
 * l'Education Numérique en Afrique (GIP ENA)
 *
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */




#include "UBBoardPaletteManager.h"

#include "frameworks/UBPlatformUtils.h"
#include "frameworks/UBFileSystemUtils.h"

#include "core/UBApplication.h"
#include "core/UBApplicationController.h"
#include "core/UBSettings.h"
#include "core/UBSetting.h"
#include "core/UBDisplayManager.h"

#include "document/UBDocument.h"

#include "gui/UBMainWindow.h"

#include "core/UB.h"

// Hold the line button this long to open its sub-tool pop-out, matching the
// desktop palette's PROPERTY_PALETTE_TIMER.
#define LINE_TOOLS_PALETTE_TIMER 350
#include "gui/UBStylusPalette.h"
#include "gui/UBKeyboardPalette.h"
#include "gui/UBToolWidget.h"
#include "gui/UBZoomPalette.h"
#include "gui/UBWebToolsPalette.h"
#include "gui/UBActionPalette.h"
#include "gui/UBBackgroundPalette.h"
#include "gui/UBFavoriteToolPalette.h"
#include "gui/UBStartupHintsPalette.h"
#include "gui/UBPageNavigationWidget.h"

#include "web/UBWebController.h"

#include "desktop/UBDesktopAnnotationController.h"

#include "network/UBNetworkAccessManager.h"
#include "network/UBServerXMLHttpRequest.h"

#include "domain/UBGraphicsScene.h"
#include "domain/UBGraphicsPixmapItem.h"

#include "document/UBDocumentProxy.h"
#include "podcast/UBPodcastController.h"
#include "board/UBDrawingController.h"

#include "tools/UBToolsManager.h"

#include "UBBoardController.h"

#include "document/UBDocumentController.h"

#include "core/UBPersistenceManager.h"
#include "core/memcheck.h"

inline constexpr int longpress_interval = 350;

UBBoardPaletteManager::UBBoardPaletteManager(QWidget* container, UBBoardController* pBoardController)
    : QObject(container)
    , mKeyboardPalette(0)
    , mWebToolsCurrentPalette(0)
    , mContainer(container)
    , mBoardControler(pBoardController)
    , mStylusPalette(0)
    , mLineToolsPalette(0)
    , mPendingLineButtonPressed(false)
    , mLineArrowClicked(false)
    , mZoomPalette(0)
    , mTipPalette(0)
    , mLeftPalette(NULL)
    , mRightPalette(NULL)
    , mBackgroundsPalette(0)
    , mToolsPalette(0)
    , mAddItemPalette(0)
    , mErasePalette(NULL)
    , mPagePalette(NULL)
    , mPendingPageButtonPressed(false)
    , mPendingZoomButtonPressed(false)
    , mPendingHandButtonPressed(false)
    , mPendingEraseButtonPressed(false)
    , mpPageNavigWidget(NULL)
    , mpCachePropWidget(NULL)
    , mpDownloadWidget(NULL)
    , mDownloadInProgress(false)
{
    setupPalettes();
    connectPalettes();
}


UBBoardPaletteManager::~UBBoardPaletteManager()
{

// mAddedItemPalette is delete automatically because of is parent
// that changes depending on the mode

// mMainWindow->centralWidget is the parent of mStylusPalette
// do not delete this here.
}

void UBBoardPaletteManager::initPalettesPosAtStartup()
{
    mStylusPalette->initPosition();
}

void UBBoardPaletteManager::setupLayout()
{

}

/**
 * \brief Set up the dock palette widgets
 */
void UBBoardPaletteManager::setupLineToolsPalette()
{
    UBMainWindow* mainWindow = UBApplication::mainWindow;

    // The line button gains the pop-out arrow the desktop palette uses for its
    // own sub-tools.
    QIcon lineIcon;
    lineIcon.addFile(":images/stylusPalette/lineArrow.svg", QSize(), QIcon::Normal, QIcon::Off);
    lineIcon.addFile(":images/stylusPalette/lineOnArrow.svg", QSize(), QIcon::Normal, QIcon::On);
    mainWindow->actionLine->setIcon(lineIcon);

    mLineToolsPalette = new UBActionPalette(Qt::TopLeftCorner, mContainer, Qt::Horizontal);
    mLineToolsPalette->setObjectName("LineToolsPalette");

    // No WA_StyledBackground here. The desktop property palettes set it because
    // the themes style them by object name; with no rule of its own this
    // palette would paint no background at all and show as an empty box.
    // Leaving it clear lets UBFloatingPalette paint the same frame the stylus
    // palette gets.

    QList<QAction*> actions;
    actions << mainWindow->actionLine;
    actions << mainWindow->actionRectangle;
    actions << mainWindow->actionFloodFill;

    // Creating a button reassigns its action's "id" property, which the stylus
    // palette uses to identify the tool a double click landed on. Put the line
    // action's own id back afterwards.
    const QVariant lineActionId = mainWindow->actionLine->property("id");

    mLineToolsPalette->setActions(actions);
    mLineToolsPalette->setButtonIconSize(QSize(42, 42));
    mLineToolsPalette->adjustSizeAndPosition();

    if (lineActionId.isValid())
        mainWindow->actionLine->setProperty("id", lineActionId);

    // Deliberately not grouped here: the three actions belong in the stylus
    // palette's group so that they stay exclusive with the other tools. A
    // QAction can only be in one group, and grouping them here would take them
    // out of it.
    if (QActionGroup* stylusGroup = mainWindow->actionLine->actionGroup())
    {
        stylusGroup->addAction(mainWindow->actionRectangle);
        stylusGroup->addAction(mainWindow->actionFloodFill);
    }

    mLineToolsPalette->hide();

    mLineToolsHoldTimer.setSingleShot(true);
    connect(&mLineToolsHoldTimer, SIGNAL(timeout()), this, SLOT(lineActionReleased()));

    if (UBActionPaletteButton* lineButton = mStylusPalette->getButtonFromAction(mainWindow->actionLine))
    {
        connect(lineButton, SIGNAL(pressed()), this, SLOT(lineActionPressed()));
        connect(lineButton, SIGNAL(released()), this, SLOT(lineActionReleased()));
    }

    // Close the pop-out once a sub-tool has been chosen, and keep the stylus
    // palette's button showing which one is current.
    connect(UBDrawingController::drawingController(), SIGNAL(stylusToolChanged(int,int)),
            this, SLOT(updateLineToolButton()));
}

bool UBBoardPaletteManager::lineSubtoolArrowPressed() const
{
    UBActionPaletteButton* button = mStylusPalette->getButtonFromAction(UBApplication::mainWindow->actionLine);

    if (!button)
        return false;

    const QPoint localPosition = button->mapFromGlobal(QCursor::pos());
    const int arrowExtent = qMax(14, qRound(qMin(button->width(), button->height()) * 0.36));
    const QRect arrowRegion(button->width() - arrowExtent, button->height() - arrowExtent,
                            arrowExtent, arrowExtent);
    return arrowRegion.contains(localPosition);
}

void UBBoardPaletteManager::lineActionPressed()
{
    mLineButtonPressedTime = QTime::currentTime();
    mPendingLineButtonPressed = true;

    if (lineSubtoolArrowPressed())
    {
        mLineArrowClicked = true;
        lineActionReleased();
        return;
    }

    mLineToolsHoldTimer.start(LINE_TOOLS_PALETTE_TIMER);
}

void UBBoardPaletteManager::lineActionReleased()
{
    mLineToolsHoldTimer.stop();

    if (mPendingLineButtonPressed)
    {
        if (mLineArrowClicked
            || mLineButtonPressedTime.msecsTo(QTime::currentTime()) > LINE_TOOLS_PALETTE_TIMER - 100)
        {
            toggleLineToolsPalette();
        }
        else
        {
            UBApplication::mainWindow->actionLine->trigger();
        }

        mPendingLineButtonPressed = false;
    }

    mLineArrowClicked = false;
}

void UBBoardPaletteManager::toggleLineToolsPalette()
{
    if (!mLineToolsPalette)
        return;

    const bool show = !mLineToolsPalette->isVisible();

    if (show)
        positionLineToolsPalette();

    mLineToolsPalette->setVisible(show);

    if (show)
        mLineToolsPalette->raise();
}

void UBBoardPaletteManager::positionLineToolsPalette()
{
    QPoint position = mStylusPalette->geometry().topLeft();
    const QList<QAction*> actions = mStylusPalette->actions();
    int offset = 0;

    foreach (QAction* action, actions)
    {
        if (action == UBApplication::mainWindow->actionLine)
        {
            const int index = actions.indexOf(action);
            offset = index * (mStylusPalette->buttonSize().height() + 2 * mStylusPalette->border() + 6);
            break;
        }
    }

    // Prefer the right of the stylus palette, falling back to its left when
    // there is not enough room.
    if (position.x() <= mContainer->width() - (mLineToolsPalette->width() + mStylusPalette->width() + 20))
    {
        position += QPoint(mStylusPalette->width(), offset);
    }
    else
    {
        position += QPoint(-mLineToolsPalette->width(), offset);
    }

    mLineToolsPalette->setCustomPosition(true);
    mLineToolsPalette->move(position);
}

void UBBoardPaletteManager::updateLineToolButton()
{
    if (!mLineToolsPalette)
        return;

    const int tool = UBDrawingController::drawingController()->stylusTool();
    QString off{":images/stylusPalette/lineArrow.svg"};
    QString on{":images/stylusPalette/lineOnArrow.svg"};

    // The three sub-tools are exclusive, so selecting one unchecks the line
    // action. Reflect the active one in the button instead, otherwise the
    // palette would show no tool selected at all.
    if (tool == UBStylusTool::Rectangle)
    {
        off = ":images/stylusPalette/rectangleArrow.svg";
        on = ":images/stylusPalette/rectangleOnArrow.svg";
    }
    else if (tool == UBStylusTool::FloodFill)
    {
        off = ":images/stylusPalette/fillArrow.svg";
        on = ":images/stylusPalette/fillOnArrow.svg";
    }

    QIcon icon;
    icon.addFile(off, QSize(), QIcon::Normal, QIcon::Off);
    icon.addFile(on, QSize(), QIcon::Normal, QIcon::On);
    UBApplication::mainWindow->actionLine->setIcon(icon);

    mLineToolsPalette->hide();
}

void UBBoardPaletteManager::setupDockPaletteWidgets()
{

    //------------------------------------------------//
    // Create the widgets for the dock palettes
    mpPageNavigWidget = new UBPageNavigationWidget();

    mpCachePropWidget = new UBCachePropertiesWidget();

    mpDownloadWidget = new UBDockDownloadWidget();

    // Add the dock palettes
    mLeftPalette = new UBLeftPalette(mContainer);

    // LEFT palette widgets
    mLeftPalette->registerWidget(mpPageNavigWidget);
    mLeftPalette->addTab(mpPageNavigWidget);

    mLeftPalette->connectSignals();
    mLeftPalette->showTabWidget(0);

    mRightPalette = new UBRightPalette(mContainer);
    // RIGHT palette widgets
    mpFeaturesWidget = new UBFeaturesWidget();
    mRightPalette->registerWidget(mpFeaturesWidget);
    mRightPalette->addTab(mpFeaturesWidget);

    // The cache widget will be visible only if a cache is put on the page
    mRightPalette->registerWidget(mpCachePropWidget);

    //  The download widget will be part of the right palette but
    //  will become visible only when the first download starts
    mRightPalette->registerWidget(mpDownloadWidget);
    mRightPalette->connectSignals();
    changeMode(eUBDockPaletteWidget_BOARD, true);

    // Hide the tabs that must be hidden
    mRightPalette->removeTab(mpDownloadWidget);
    mRightPalette->removeTab(mpCachePropWidget);

}

void UBBoardPaletteManager::slot_changeMainMode(UBApplicationController::MainMode mainMode)
{
//    Board = 0, Internet, Document, Tutorial, WebDocument

    switch( mainMode )
    {
        case UBApplicationController::Board:
            {
                // call changeMode only when switch NOT from desktop mode
                if(!UBApplication::applicationController->isShowingDesktop())
                    changeMode(eUBDockPaletteWidget_BOARD);
            }
            break;
        case UBApplicationController::Internet:
            changeMode(eUBDockPaletteWidget_WEB);
            break;

        case UBApplicationController::Document:
            changeMode(eUBDockPaletteWidget_DOCUMENT);
            break;

        default:
            {
                if (UBPlatformUtils::hasVirtualKeyboard() && mKeyboardPalette != NULL)
                    mKeyboardPalette->hide();
            }
            break;
    }
}

void UBBoardPaletteManager::slot_changeDesktopMode(bool isDesktop)
{
    UBApplicationController::MainMode currMode = UBApplication::applicationController->displayMode();
    if(!isDesktop)
    {
        switch( currMode )
        {
            case UBApplicationController::Board:
                changeMode(eUBDockPaletteWidget_BOARD);
                break;

            default:
                break;
        }
    }
    else
        changeMode(eUBDockPaletteWidget_DESKTOP);
}

void UBBoardPaletteManager::setupPalettes()
{

    if (UBPlatformUtils::hasVirtualKeyboard())
    {
        mKeyboardPalette = new UBKeyboardPalette(0);
#ifndef Q_OS_WIN
        connect(mKeyboardPalette, SIGNAL(closed()), mKeyboardPalette, SLOT(onDeactivated()));
#endif
    }


    setupDockPaletteWidgets();


    // Add the other palettes
    mStylusPalette = new UBStylusPalette(mContainer, UBSettings::settings()->appToolBarOrientationVertical->get().toBool() ? Qt::Vertical : Qt::Horizontal);
    connect(mStylusPalette, SIGNAL(stylusToolDoubleClicked(int)), UBApplication::boardController, SLOT(stylusToolDoubleClicked(int)));
    mStylusPalette->show(); // always show stylus palette at startup

    setupLineToolsPalette();

    mZoomPalette = new UBZoomPalette(mContainer);

    mStylusPalette->stackUnder(mZoomPalette);

    mTipPalette = new UBStartupHintsPalette(mContainer);

    mBackgroundsPalette = new UBBackgroundPalette(mContainer);
    mBackgroundsPalette->setButtonIconSize(QSize(128, 128));
    mBackgroundsPalette->groupActions();
    mBackgroundsPalette->setClosable(true);
    mBackgroundsPalette->setAutoClose(false);
    mBackgroundsPalette->adjustSizeAndPosition();
    mBackgroundsPalette->hide();

    QList<QAction*> addItemActions;

    addItemActions << UBApplication::mainWindow->actionAddItemToCurrentPage;
    addItemActions << UBApplication::mainWindow->actionAddItemToNewPage;
    addItemActions << UBApplication::mainWindow->actionAddItemToLibrary;

    mAddItemPalette = new UBActionPalette(addItemActions, Qt::Horizontal, mContainer);
    mAddItemPalette->setObjectName("AddItemPalette");
    mAddItemPalette->setButtonIconSize(QSize(128, 128));
    mAddItemPalette->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    mAddItemPalette->groupActions();
    mAddItemPalette->setClosable(true);
    mAddItemPalette->adjustSizeAndPosition();
    mAddItemPalette->hide();

    QList<QAction*> eraseActions;

    eraseActions << UBApplication::mainWindow->actionEraseAnnotations;
    eraseActions << UBApplication::mainWindow->actionEraseItems;
    eraseActions << UBApplication::mainWindow->actionClearPage;
    eraseActions << UBApplication::mainWindow->actionEraseBackground;

    mErasePalette = new UBActionPalette(eraseActions, Qt::Horizontal , mContainer);
    mErasePalette->setButtonIconSize(QSize(128, 128));
    mErasePalette->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    mErasePalette->groupActions();
    mErasePalette->setClosable(true);
    mErasePalette->adjustSizeAndPosition();
    mErasePalette->hide();

    QList<QAction*> pageActions;

    pageActions << UBApplication::mainWindow->actionNewPage;
    pageActions << UBApplication::mainWindow->actionDuplicatePage;
    pageActions << UBApplication::mainWindow->actionImportPage;

    mPagePalette = new UBActionPalette(pageActions, Qt::Horizontal , mContainer);
    mPagePalette->setButtonIconSize(QSize(128, 128));
    mPagePalette->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    mPagePalette->groupActions();
    mPagePalette->setClosable(true);
    mPagePalette->adjustSizeAndPosition();
    mPagePalette->hide();

    connect(UBSettings::settings()->appToolBarOrientationVertical, SIGNAL(changed(QVariant)), this, SLOT(changeStylusPaletteOrientation(QVariant)));
}

void UBBoardPaletteManager::pagePaletteButtonPressed()
{
    mPageButtonPressedTime = QTime::currentTime();

    mPendingPageButtonPressed = true;
    QTimer::singleShot(longpress_interval, this, SLOT(pagePaletteButtonReleased()));
}


void UBBoardPaletteManager::pagePaletteButtonReleased()
{
    if (mPendingPageButtonPressed)
    {
        if( mPageButtonPressedTime.msecsTo(QTime::currentTime()) > longpress_interval-100)
        {
            // The palette is reinstanciated because the duplication depends on the current scene
            delete(mPagePalette);
            mPagePalette = 0;
            QList<QAction*>pageActions;
            pageActions << UBApplication::mainWindow->actionNewPage;
            pageActions << UBApplication::mainWindow->actionDuplicatePage;
            pageActions << UBApplication::mainWindow->actionImportPage;

            mPagePalette = new UBActionPalette(pageActions, Qt::Horizontal , mContainer);
            mPagePalette->setButtonIconSize(QSize(128, 128));
            mPagePalette->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            mPagePalette->groupActions();
            mPagePalette->setClosable(true);

            // As we recreate the pagePalette every time, we must reconnect the slots
            connect(UBApplication::mainWindow->actionNewPage, SIGNAL(triggered()), mPagePalette, SLOT(close()));
            connect(UBApplication::mainWindow->actionDuplicatePage, SIGNAL(triggered()), mPagePalette, SLOT(close()));
            connect(UBApplication::mainWindow->actionImportPage, SIGNAL(triggered()), mPagePalette, SLOT(close()));
            connect(mPagePalette, SIGNAL(closed()), this, SLOT(pagePaletteClosed()));

            togglePagePalette(true);
        }
        else
        {
            UBApplication::mainWindow->actionNewPage->trigger();
        }

        mPendingPageButtonPressed = false;
    }
}

void UBBoardPaletteManager::erasePaletteButtonPressed()
{
    mEraseButtonPressedTime = QTime::currentTime();

    mPendingEraseButtonPressed = true;
    QTimer::singleShot(longpress_interval, this, SLOT(erasePaletteButtonReleased()));
}


void UBBoardPaletteManager::erasePaletteButtonReleased()
{
    if (mPendingEraseButtonPressed)
    {
        if( mEraseButtonPressedTime.msecsTo(QTime::currentTime()) > longpress_interval - 100)
        {
            toggleErasePalette(true);
        }
        else
        {
            UBApplication::mainWindow->actionClearPage->trigger();
        }

        mPendingEraseButtonPressed = false;
    }
}



void UBBoardPaletteManager::linkClicked(const QUrl& url)
{
      UBApplication::applicationController->showInternet();
      UBApplication::webController->loadUrl(url);
}


void UBBoardPaletteManager::purchaseLinkActivated(const QString& link)
{
    UBApplication::applicationController->showInternet();
    UBApplication::webController->loadUrl(QUrl(link));
}

void UBBoardPaletteManager::connectPalettes()
{
    connect(UBApplication::mainWindow->actionStylus, SIGNAL(toggled(bool)), this, SLOT(toggleStylusPalette(bool)));

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    foreach(QObject *widget, UBApplication::mainWindow->actionZoomIn->associatedObjects())
#else
    foreach(QWidget *widget, UBApplication::mainWindow->actionZoomIn->associatedWidgets())
#endif
    {
        QAbstractButton *button = qobject_cast<QAbstractButton*>(widget);
        if (button)
        {
            connect(button, SIGNAL(pressed()), this, SLOT(zoomButtonPressed()));
            connect(button, SIGNAL(released()), this, SLOT(zoomButtonReleased()));
        }
    }

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    foreach(QObject *widget, UBApplication::mainWindow->actionZoomOut->associatedObjects())
#else
    foreach(QWidget *widget, UBApplication::mainWindow->actionZoomOut->associatedWidgets())
#endif
    {
        QAbstractButton *button = qobject_cast<QAbstractButton*>(widget);
        if (button)
        {
            connect(button, SIGNAL(pressed()), this, SLOT(zoomButtonPressed()));
            connect(button, SIGNAL(released()), this, SLOT(zoomButtonReleased()));
        }
    }

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    foreach(QObject *widget, UBApplication::mainWindow->actionHand->associatedObjects())
#else
    foreach(QWidget *widget, UBApplication::mainWindow->actionHand->associatedWidgets())
#endif
    {
        QAbstractButton *button = qobject_cast<QAbstractButton*>(widget);
        if (button)
        {
            connect(button, SIGNAL(pressed()), this, SLOT(handButtonPressed()));
            connect(button, SIGNAL(released()), this, SLOT(handButtonReleased()));
        }
    }

    connect(UBApplication::mainWindow->actionBackgrounds, SIGNAL(toggled(bool)), this, SLOT(toggleBackgroundPalette(bool)));
    connect(mBackgroundsPalette, SIGNAL(closed()), this, SLOT(backgroundPaletteClosed()));

    connect(UBApplication::mainWindow->actionPodcast, SIGNAL(triggered(bool)), this, SLOT(tooglePodcastPalette(bool)));

    connect(UBApplication::mainWindow->actionAddItemToCurrentPage, SIGNAL(triggered()), this, SLOT(addItemToCurrentPage()));
    connect(UBApplication::mainWindow->actionAddItemToNewPage, SIGNAL(triggered()), this, SLOT(addItemToNewPage()));
    connect(UBApplication::mainWindow->actionAddItemToLibrary, SIGNAL(triggered()), this, SLOT(addItemToLibrary()));

    connect(UBApplication::mainWindow->actionEraseItems, SIGNAL(triggered()), mErasePalette, SLOT(close()));
    connect(UBApplication::mainWindow->actionEraseAnnotations, SIGNAL(triggered()), mErasePalette, SLOT(close()));
    connect(UBApplication::mainWindow->actionClearPage, SIGNAL(triggered()), mErasePalette, SLOT(close()));
    connect(UBApplication::mainWindow->actionEraseBackground,SIGNAL(triggered()),mErasePalette,SLOT(close()));
    connect(mErasePalette, SIGNAL(closed()), this, SLOT(erasePaletteClosed()));

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    foreach(QObject *widget, UBApplication::mainWindow->actionErase->associatedObjects())
#else
    foreach(QWidget *widget, UBApplication::mainWindow->actionErase->associatedWidgets())
#endif
    {
        QAbstractButton *button = qobject_cast<QAbstractButton*>(widget);
        if (button)
        {
            connect(button, SIGNAL(pressed()), this, SLOT(erasePaletteButtonPressed()));
            connect(button, SIGNAL(released()), this, SLOT(erasePaletteButtonReleased()));
        }
    }

    connect(UBApplication::mainWindow->actionNewPage, SIGNAL(triggered()), mPagePalette, SLOT(close()));
    connect(UBApplication::mainWindow->actionDuplicatePage, SIGNAL(triggered()), mPagePalette, SLOT(close()));
    connect(UBApplication::mainWindow->actionImportPage, SIGNAL(triggered()), mPagePalette, SLOT(close()));
    connect(mPagePalette, SIGNAL(closed()), this, SLOT(pagePaletteClosed()));

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    foreach(QObject *widget, UBApplication::mainWindow->actionPages->associatedObjects())
#else
    foreach(QWidget *widget, UBApplication::mainWindow->actionPages->associatedWidgets())
#endif
    {
        QAbstractButton *button = qobject_cast<QAbstractButton*>(widget);
        if (button)
        {
            connect(button, SIGNAL(pressed()), this, SLOT(pagePaletteButtonPressed()));
            connect(button, SIGNAL(released()), this, SLOT(pagePaletteButtonReleased()));
        }
    }

}


bool isFirstResized = true;
void UBBoardPaletteManager::containerResized()
{
    int innerMargin = UBSettings::boardMargin;

    int userLeft = innerMargin;
    int userWidth = mContainer->width() - (2 * innerMargin);
    int userTop = innerMargin;
    int userHeight = mContainer->height() - (2 * innerMargin);

    if(mStylusPalette)
    {
        mStylusPalette->move(userLeft, userTop);
        mStylusPalette->adjustSizeAndPosition();
        mStylusPalette->initPosition();
    }

    if(mZoomPalette)
    {
        mZoomPalette->move(userLeft + userWidth - mZoomPalette->width()
                , userTop + userHeight /*- mPageNumberPalette->height()*/ - innerMargin - mZoomPalette->height());
        mZoomPalette->adjustSizeAndPosition();
        mZoomPalette->refreshPalette();
    }

    if (isFirstResized && mKeyboardPalette && mKeyboardPalette->parent() == UBApplication::boardController->controlContainer())
    {
        isFirstResized = false;
        mKeyboardPalette->move(userLeft + (userWidth - mKeyboardPalette->width())/2,
                               userTop + (userHeight - mKeyboardPalette->height())/2);
        mKeyboardPalette->adjustSizeAndPosition();
    }
}


void UBBoardPaletteManager::activeSceneChanged()
{
    std::shared_ptr<UBGraphicsScene> activeScene =  UBApplication::boardController->activeScene();
    int pageIndex = UBApplication::boardController->activeSceneIndex();

    if (mStylusPalette)
        connect(mStylusPalette, SIGNAL(mouseEntered()), activeScene.get(), SLOT(hideTool()));

    if (mpPageNavigWidget)
    {
        mpPageNavigWidget->setPageNumber(UBDocumentContainer::pageFromSceneIndex(pageIndex), UBApplication::boardController->activeDocument()->pageCount());
    }

    if (mZoomPalette)
        connect(mZoomPalette, SIGNAL(mouseEntered()), activeScene.get(), SLOT(hideTool()));

    if (mBackgroundsPalette) {
        connect(mBackgroundsPalette, SIGNAL(mouseEntered()), activeScene.get(), SLOT(hideTool()));
        mBackgroundsPalette->refresh();
    }
}


void UBBoardPaletteManager::toggleBackgroundPalette(bool checked)
{
    mBackgroundsPalette->setVisible(checked);

    if (checked)
    {
        UBApplication::mainWindow->actionErase->setChecked(false);
        UBApplication::mainWindow->actionNewPage->setChecked(false);

        mBackgroundsPalette->adjustSizeAndPosition();
        mBackgroundsPalette->move((mContainer->width() - mBackgroundsPalette->width()) / 2,
            (mContainer->height() - mBackgroundsPalette->height()) / 5);
    }
}


void UBBoardPaletteManager::backgroundPaletteClosed()
{
    UBApplication::mainWindow->actionBackgrounds->setChecked(false);
}


void UBBoardPaletteManager::toggleStylusPalette(bool checked)
{
    mStylusPalette->setVisible(checked);
}


void UBBoardPaletteManager::toggleErasePalette(bool checked)
{
    mErasePalette->setVisible(checked);
    if (checked)
    {
        UBApplication::mainWindow->actionBackgrounds->setChecked(false);
        UBApplication::mainWindow->actionNewPage->setChecked(false);

        mErasePalette->adjustSizeAndPosition();
        mErasePalette->move((mContainer->width() - mErasePalette->width()) / 2,
            (mContainer->height() - mErasePalette->height()) / 5);
    }
}


void UBBoardPaletteManager::erasePaletteClosed()
{
    UBApplication::mainWindow->actionErase->setChecked(false);
}


void UBBoardPaletteManager::togglePagePalette(bool checked)
{
    mPagePalette->setVisible(checked);
    if (checked)
    {
        UBApplication::mainWindow->actionBackgrounds->setChecked(false);
        UBApplication::mainWindow->actionErase->setChecked(false);

        mPagePalette->adjustSizeAndPosition();
        mPagePalette->move((mContainer->width() - mPagePalette->width()) / 2,
            (mContainer->height() - mPagePalette->height()) / 5);
    }
}


void UBBoardPaletteManager::pagePaletteClosed()
{
    UBApplication::mainWindow->actionPages->setChecked(false);
}


void UBBoardPaletteManager::tooglePodcastPalette(bool checked)
{
    UBPodcastController::instance()->toggleRecordingPalette(checked);
}


void UBBoardPaletteManager::addItem(const QUrl& pUrl)
{
    mItemUrl = pUrl;
    mPixmap = QPixmap();
    mPos = QPointF(0, 0);
    mScaleFactor = 1.;

    mAddItemPalette->show();
    mAddItemPalette->adjustSizeAndPosition();

    mAddItemPalette->move((mContainer->width() - mAddItemPalette->width()) / 2,
        (mContainer->height() - mAddItemPalette->height()) / 5);
}

void UBBoardPaletteManager::changeMode(eUBDockPaletteWidgetMode newMode, bool isInit)
{
    bool rightPaletteVisible = mRightPalette->switchMode(newMode);
    bool leftPaletteVisible = mLeftPalette->switchMode(newMode);

    switch( newMode )
    {
        case eUBDockPaletteWidget_BOARD:
            {
                // On Application start up the mAddItemPalette isn't initialized yet
                if(mAddItemPalette){
                    mAddItemPalette->setParent(UBApplication::boardController->controlContainer());
                }
                mLeftPalette->assignParent(mContainer);
                mRightPalette->assignParent(mContainer);
                mRightPalette->stackUnder(mStylusPalette);
                mLeftPalette->stackUnder(mStylusPalette);
                if (UBPlatformUtils::hasVirtualKeyboard()
                    && mKeyboardPalette != NULL
                    && UBSettings::settings()->useSystemOnScreenKeyboard->get().toBool() == false)
                {
                    if(mKeyboardPalette->m_isVisible) {
                        mKeyboardPalette->hide();
                        mKeyboardPalette->setParent(UBApplication::boardController->controlContainer());
                        mKeyboardPalette->show();
                    }
                    else
                        mKeyboardPalette->setParent(UBApplication::boardController->controlContainer());
                }

                mLeftPalette->setVisible(leftPaletteVisible);
                mRightPalette->setVisible(rightPaletteVisible);
#ifdef Q_OS_WIN
                if (rightPaletteVisible)
                    mRightPalette->setAdditionalVOffset(0);
#endif

                if( !isInit )
                    containerResized();
                if (mWebToolsCurrentPalette)
                    mWebToolsCurrentPalette->hide();
            }
            break;

        case eUBDockPaletteWidget_DESKTOP:
            {
                mAddItemPalette->setParent((QWidget*)UBApplication::applicationController->uninotesController()->drawingView());
                mLeftPalette->assignParent((QWidget*)UBApplication::applicationController->uninotesController()->drawingView());
                mRightPalette->assignParent((QWidget*)UBApplication::applicationController->uninotesController()->drawingView());
                mStylusPalette->raise();

                if (UBPlatformUtils::hasVirtualKeyboard()
                    && mKeyboardPalette != NULL
                    && UBSettings::settings()->useSystemOnScreenKeyboard->get().toBool() == false)
                {

                    if(mKeyboardPalette->m_isVisible)
                    {
                        mKeyboardPalette->hide();
#ifndef Q_OS_LINUX
                        mKeyboardPalette->setParent((QWidget*)UBApplication::applicationController->uninotesController()->drawingView());
#else
                        mKeyboardPalette->setParent(0);
#endif
#ifdef Q_OS_OSX
                        mKeyboardPalette->setWindowFlags(Qt::Dialog | Qt::Popup | Qt::FramelessWindowHint);
#endif
                        mKeyboardPalette->show();
                    }
                    else
// In linux keyboard in desktop mode have to allways be with null parent
#ifdef Q_OS_LINUX
                        mKeyboardPalette->setParent(0);
#else
                        mKeyboardPalette->setParent((QWidget*)UBApplication::applicationController->uninotesController()->drawingView());
#endif //Q_OS_LINUX
#ifdef Q_OS_OSX
                        mKeyboardPalette->setWindowFlags(Qt::Dialog | Qt::Popup | Qt::FramelessWindowHint);
#endif

                }

                mLeftPalette->setVisible(leftPaletteVisible);
                mRightPalette->setVisible(rightPaletteVisible);
#ifdef Q_OS_WIN
                if (rightPaletteVisible && UBSettings::settings()->appToolBarPositionedAtTop->get().toBool())
                    mRightPalette->setAdditionalVOffset(30);
#endif

                if(!isInit)
                    UBApplication::applicationController->uninotesController()->TransparentWidgetResized();

                if (mWebToolsCurrentPalette)
                    mWebToolsCurrentPalette->hide();
            }
            break;

        case eUBDockPaletteWidget_WEB:
            {
                mAddItemPalette->setParent(UBApplication::mainWindow);
                if (UBPlatformUtils::hasVirtualKeyboard()
                    && mKeyboardPalette != NULL
                    && UBSettings::settings()->useSystemOnScreenKeyboard->get().toBool() == false)
                {
//                    tmp variable?
//                    WBBrowserWindow* brWnd = UBApplication::webController->GetCurrentWebBrowser();

                    if(mKeyboardPalette->m_isVisible)
                    {
                        mKeyboardPalette->hide();
                        mKeyboardPalette->setParent(UBApplication::mainWindow);
                        mKeyboardPalette->show();
                    }
                    else
                        mKeyboardPalette->setParent(UBApplication::mainWindow);
                }

            }
            break;

        case eUBDockPaletteWidget_DOCUMENT:
            {
                mLeftPalette->setVisible(leftPaletteVisible);
                mRightPalette->setVisible(rightPaletteVisible);
                mLeftPalette->assignParent(UBApplication::documentController->controlView());
                mRightPalette->assignParent(UBApplication::documentController->controlView());
                if (UBPlatformUtils::hasVirtualKeyboard()
                    && mKeyboardPalette != NULL
                    && UBSettings::settings()->useSystemOnScreenKeyboard->get().toBool() == false)
                {

                    if(mKeyboardPalette->m_isVisible)
                    {
                        mKeyboardPalette->hide();
                        mKeyboardPalette->setParent(UBApplication::documentController->controlView());
                        mKeyboardPalette->show();
                    }
                    else
                        mKeyboardPalette->setParent(UBApplication::documentController->controlView());
                }
                if (mWebToolsCurrentPalette)
                    mWebToolsCurrentPalette->hide();
            }
            break;

        default:
            {
                mLeftPalette->setVisible(leftPaletteVisible);
                mRightPalette->setVisible(rightPaletteVisible);
                mLeftPalette->assignParent(0);
                mRightPalette->assignParent(0);
                if (UBPlatformUtils::hasVirtualKeyboard()
                    && mKeyboardPalette != NULL
                    && UBSettings::settings()->useSystemOnScreenKeyboard->get().toBool() == false)
                {

                    if(mKeyboardPalette->m_isVisible)
                    {
                        mKeyboardPalette->hide();
                        mKeyboardPalette->setParent(0);
                        mKeyboardPalette->show();
                    }
                    else
                        mKeyboardPalette->setParent(0);
                }
            }
            break;
    }

    if( !isInit )
        UBApplication::boardController->notifyPageChanged();
}

void UBBoardPaletteManager::addItem(const QPixmap& pPixmap, const QPointF& pos,  qreal scaleFactor, const QUrl& sourceUrl)
{
    mItemUrl = sourceUrl;
    mPixmap = pPixmap;
    mPos = pos;
    mScaleFactor = scaleFactor;

    mAddItemPalette->show();
    mAddItemPalette->adjustSizeAndPosition();

    mAddItemPalette->move((mContainer->width() - mAddItemPalette->width()) / 2,
        (mContainer->height() - mAddItemPalette->height()) / 5);
}


void UBBoardPaletteManager::addItemToCurrentPage()
{
    UBApplication::applicationController->showBoard();
    mAddItemPalette->hide();

    if (mPixmap.isNull())
    {
        UBApplication::boardController->downloadURL(mItemUrl);
    }
    else
    {
        UBGraphicsPixmapItem* item = UBApplication::boardController->activeScene()->addPixmap(mPixmap, NULL, mPos, mScaleFactor);

        item->setSelected(true);

        UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
    }
}


void UBBoardPaletteManager::addItemToNewPage()
{
    UBApplication::boardController->addScene();
    addItemToCurrentPage();
}


void UBBoardPaletteManager::addItemToLibrary()
{
    QByteArray data;

    if(mPixmap.isNull())
    {
        QFile file(mItemUrl.toLocalFile());

        if (file.open(QFile::ReadOnly))
        {
            data = file.readAll();
            file.close();
        }
    }
    else
    {
        if(mScaleFactor != 1.)
        {
             mPixmap = mPixmap.scaled(mScaleFactor * mPixmap.width(), mScaleFactor* mPixmap.height()
                     , Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        QBuffer buffer(&data);
        mPixmap.save(&buffer, "png");
    }

    if(!data.isEmpty())
    {
        QDateTime now = QDateTime::currentDateTime();
        QString capturedName  = tr("CapturedImage") + "-" + now.toString("dd-MM-yyyy hh-mm-ss") + ".png";
        mpFeaturesWidget->importImage(data, capturedName);
    }
    else
    {
        UBApplication::showMessage(tr("Error Adding Image to Library"));
    }

    mAddItemPalette->hide();
}

void UBBoardPaletteManager::zoomButtonPressed()
{
    mZoomButtonPressedTime = QTime::currentTime();

    mPendingZoomButtonPressed = true;
    QTimer::singleShot(longpress_interval, this, SLOT(zoomButtonReleased()));
}


void UBBoardPaletteManager::zoomButtonReleased()
{
    if (mPendingZoomButtonPressed)
    {
        if(mZoomButtonPressedTime.msecsTo(QTime::currentTime()) > longpress_interval - 100)
        {
            mBoardControler->zoomRestore();
        }

        mPendingZoomButtonPressed = false;
    }
}

void UBBoardPaletteManager::handButtonPressed()
{
    mHandButtonPressedTime = QTime::currentTime();

    mPendingHandButtonPressed = true;
    QTimer::singleShot(longpress_interval, this, SLOT(handButtonReleased()));
}


void UBBoardPaletteManager::handButtonReleased()
{
    if (mPendingHandButtonPressed)
    {
        if(mHandButtonPressedTime.msecsTo(QTime::currentTime()) > longpress_interval - 100)
        {
            mBoardControler->centerRestore();
        }

        mPendingHandButtonPressed = false;
    }
}

void UBBoardPaletteManager::showVirtualKeyboard(bool show)
{
    if (mKeyboardPalette)
        mKeyboardPalette->setVisible(show);
}

void UBBoardPaletteManager::changeStylusPaletteOrientation(QVariant var)
{
    bool bVertical = var.toBool();
    bool bVisible = mStylusPalette->isVisible();

    // Clean the old palette
    if(NULL != mStylusPalette)
    {
        delete mStylusPalette;
        mStylusPalette = NULL;
    }

    // Create the new palette
    if(bVertical)
    {
        mStylusPalette = new UBStylusPalette(mContainer, Qt::Vertical);
    }
    else
    {
        mStylusPalette = new UBStylusPalette(mContainer, Qt::Horizontal);
    }

    connect(mStylusPalette, SIGNAL(stylusToolDoubleClicked(int)), UBApplication::boardController, SLOT(stylusToolDoubleClicked(int)));
    mStylusPalette->setVisible(bVisible); // always show stylus palette at startup
}


void UBBoardPaletteManager::refreshPalettes()
{
    mRightPalette->update();
    mLeftPalette->update();
}

void UBBoardPaletteManager::startDownloads()
{
    if(!mDownloadInProgress)
    {
        mDownloadInProgress = true;
        mpDownloadWidget->setVisibleState(true);
        mRightPalette->addTab(mpDownloadWidget);
    }
}

void UBBoardPaletteManager::stopDownloads()
{
    if(mDownloadInProgress)
    {
        mDownloadInProgress = false;
        mpDownloadWidget->setVisibleState(false);
        mRightPalette->removeTab(mpDownloadWidget);
    }
}
