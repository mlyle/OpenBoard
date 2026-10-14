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




#include "UBGraphicsSvgItem.h"

#include <QtGui>

#include "UBGraphicsScene.h"
#include "UBGraphicsItemDelegate.h"
#include "UBGraphicsPixmapItem.h"

#include "core/UBApplication.h"
#include "core/UBPersistenceManager.h"

#include <QDomDocument>

namespace
{
    // Marks the group holding a library shape's geometry, and carrying its
    // colours so that SVG inheritance reaches every element inside.
    const QString sShapeBodyId = QStringLiteral("ub-body");

    QColor colorFrom(const QDomElement& element, const QString& attribute)
    {
        const QString value = element.attribute(attribute).trimmed();

        if (value.isEmpty() || value.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0)
        {
            return {};
        }

        QColor color(value);
        bool ok = false;
        const double opacity = element.attribute(attribute + QStringLiteral("-opacity")).toDouble(&ok);

        if (ok && color.isValid())
        {
            color.setAlphaF(qBound(0., opacity, 1.));
        }

        return color;
    }

    void setColorOn(QDomElement& element, const QString& attribute, const QColor& color)
    {
        const QString opacityAttribute = attribute + QStringLiteral("-opacity");

        if (!color.isValid())
        {
            element.setAttribute(attribute, QStringLiteral("none"));
            element.removeAttribute(opacityAttribute);
            return;
        }

        element.setAttribute(attribute, color.name(QColor::HexRgb));

        // SVG has no alpha in the colour itself; it goes in the opacity.
        if (color.alphaF() < 1.)
        {
            element.setAttribute(opacityAttribute, QString::number(color.alphaF(), 'g', 3));
        }
        else
        {
            element.removeAttribute(opacityAttribute);
        }
    }
}

#include "board/UBBoardController.h"

#include "frameworks/UBFileSystemUtils.h"

#include "core/memcheck.h"

UBGraphicsSvgItem::UBGraphicsSvgItem(const QString& pFilePath, QGraphicsItem* parent)
    : QGraphicsSvgItem(pFilePath, parent)
{
    init();

    QFile f(pFilePath);

    if (f.open(QIODevice::ReadOnly))
    {
        mFileData = f.readAll();
        f.close();
        mMediaAssetUuid = createMediaAssetUuid(mFileData);
    }
}

UBGraphicsSvgItem::UBGraphicsSvgItem(const QByteArray& pFileData, QGraphicsItem* parent)
    : QGraphicsSvgItem(parent)
{
    init();

    QSvgRenderer* renderer = new QSvgRenderer(pFileData, this);

    setSharedRenderer(renderer);
    mFileData = pFileData;
    mMediaAssetUuid = createMediaAssetUuid(mFileData);
}


void UBGraphicsSvgItem::init()
{
    setData(UBGraphicsItemData::ItemLayerType, UBItemLayerType::Object);

    setDelegate(new UBGraphicsItemDelegate(this, 0, GF_COMMON
                                           | GF_RESPECT_RATIO
                                           | GF_REVOLVABLE));
    UBGraphicsFlags dfl = Delegate()->ubflags();
    Delegate()->setUBFlags(dfl | GF_FLIPPABLE_ALL_AXIS | GF_REVOLVABLE);

    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);

    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    setMaximumCacheSize(boundingRect().size().toSize() * UB_MAX_ZOOM);

    setData(UBGraphicsItemData::itemLayerType, QVariant(itemLayerType::ObjectItem)); //Necessary to set if we want z value to be assigned correctly

    setData(UBGraphicsItemData::ItemCanBeSetAsBackground, true);

    UBGraphicsSvgItem::setUuid(QUuid::createUuid());
}

UBGraphicsSvgItem::~UBGraphicsSvgItem()
{
}

QList<QString> UBGraphicsSvgItem::mediaAssets() const
{
    return {UBPersistenceManager::imageDirectory + "/" + mMediaAssetUuid.toString() + ".svg"};
}

void UBGraphicsSvgItem::setMediaAsset(const QString& documentPath, const QString& mediaAsset)
{
    mMediaAssetUuid = uuidFromPath(mediaAsset);
}


QByteArray UBGraphicsSvgItem::fileData() const
{
    return mFileData;
}

QString UBGraphicsSvgItem::colourToSetting(const QColor& colour)
{
    return colour.isValid() ? colour.name(QColor::HexArgb) : QStringLiteral("none");
}

QColor UBGraphicsSvgItem::colourFromSetting(const QString& value)
{
    const QString trimmed = value.trimmed();

    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0)
    {
        return {};
    }

    return QColor(trimmed);
}

QDomElement UBGraphicsSvgItem::findShapeBody(const QDomElement& element)
{
    // QDomDocument::elementById() is documented as never returning anything, so
    // the tree is walked instead.
    if (element.attribute(QStringLiteral("id")) == sShapeBodyId)
    {
        return element;
    }

    for (QDomElement child = element.firstChildElement(); !child.isNull();
         child = child.nextSiblingElement())
    {
        const QDomElement found = findShapeBody(child);

        if (!found.isNull())
        {
            return found;
        }
    }

    return {};
}

bool UBGraphicsSvgItem::isRecolourable() const
{
    QDomDocument document;

    if (!document.setContent(mFileData))
    {
        return false;
    }

    return !findShapeBody(document.documentElement()).isNull();
}

QColor UBGraphicsSvgItem::shapeFill() const
{
    QDomDocument document;

    if (!document.setContent(mFileData))
    {
        return {};
    }

    return colorFrom(findShapeBody(document.documentElement()), QStringLiteral("fill"));
}

QColor UBGraphicsSvgItem::shapeStroke() const
{
    QDomDocument document;

    if (!document.setContent(mFileData))
    {
        return {};
    }

    return colorFrom(findShapeBody(document.documentElement()), QStringLiteral("stroke"));
}

bool UBGraphicsSvgItem::setShapeColors(const QColor& fill, const QColor& stroke)
{
    QDomDocument document;

    if (!document.setContent(mFileData))
    {
        return false;
    }

    QDomElement body = findShapeBody(document.documentElement());

    if (body.isNull())
    {
        return false;
    }

    setColorOn(body, QStringLiteral("fill"), fill);
    setColorOn(body, QStringLiteral("stroke"), stroke);

    mFileData = document.toByteArray(2);

    QSvgRenderer* renderer = new QSvgRenderer(mFileData, this);
    setSharedRenderer(renderer);
    delete mOwnedRenderer;
    mOwnedRenderer = renderer;

    // The asset file is named after a hash of the content, so recolouring gives
    // the shape a new name and the caller has to write it out again.
    mMediaAssetUuid = createMediaAssetUuid(mFileData);
    update();

    return true;
}


QVariant UBGraphicsSvgItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    QVariant newValue = Delegate()->itemChange(change, value);
    return QGraphicsSvgItem::itemChange(change, newValue);
}


void UBGraphicsSvgItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    QMimeData* pMime = new QMimeData();
    QPixmap pixmap = toPixmapItem()->pixmap();
    pMime->setImageData(pixmap.toImage());
    Delegate()->setMimeData(pMime);
    qreal k = (qreal)pixmap.width() / 100.0;

    QSize newSize((int)(pixmap.width() / k), (int)(pixmap.height() / k));

    Delegate()->setDragPixmap(pixmap.scaled(newSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    if (!Delegate()->mousePressEvent(event))
        QGraphicsSvgItem::mousePressEvent(event);
}


void UBGraphicsSvgItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (Delegate()->mouseMoveEvent(event))
    {
        // NOOP;
    }
    else
    {
        QGraphicsSvgItem::mouseMoveEvent(event);
    }
}


void UBGraphicsSvgItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    Delegate()->mouseReleaseEvent(event);
    QGraphicsSvgItem::mouseReleaseEvent(event);
}


void UBGraphicsSvgItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    // Never draw the rubber band, we draw our custom selection with the DelegateFrame
    QStyleOptionGraphicsItem styleOption = QStyleOptionGraphicsItem(*option);
    styleOption.state &= ~QStyle::State_Selected;

    QGraphicsSvgItem::paint(painter, &styleOption, widget);
    Delegate()->postpaint(painter, option, widget);
}


UBItem* UBGraphicsSvgItem::deepCopy() const
{
    UBGraphicsSvgItem* copy = new UBGraphicsSvgItem(this->fileData());

    copy->setUuid(this->uuid()); // this is OK for now as long as Widgets are imutable

    copyItemParameters(copy);

    // TODO UB 4.7... complete all members ?

    return copy;

}

void UBGraphicsSvgItem::copyItemParameters(UBItem *copy) const
{
    UBGraphicsSvgItem *cp = dynamic_cast<UBGraphicsSvgItem*>(copy);
    if (cp)
    {
        cp->setPos(this->pos());
        cp->setTransform(this->transform());
        cp->setFlag(QGraphicsItem::ItemIsMovable, true);
        cp->setFlag(QGraphicsItem::ItemIsSelectable, true);
        cp->setData(UBGraphicsItemData::ItemLayerType, this->data(UBGraphicsItemData::ItemLayerType));
        cp->setData(UBGraphicsItemData::ItemLocked, this->data(UBGraphicsItemData::ItemLocked));
        cp->setData(UBGraphicsItemData::ItemIsHiddenOnDisplay, this->data(UBGraphicsItemData::ItemIsHiddenOnDisplay));
        cp->setData(UBGraphicsItemData::ItemOwnZValue, this->data(UBGraphicsItemData::ItemOwnZValue));
        cp->setZValue(this->zValue());
        cp->mMediaAssetUuid = mMediaAssetUuid;
    }
}

void UBGraphicsSvgItem::setRenderingQuality(RenderingQuality pRenderingQuality)
{
    UBItem::setRenderingQuality(pRenderingQuality);

    if (pRenderingQuality == RenderingQualityHigh)
    {
        setCacheMode(QGraphicsItem::NoCache);
    }
    else
    {
        setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    }
}


std::shared_ptr<UBGraphicsScene> UBGraphicsSvgItem::scene()
{
    auto scenePtr = dynamic_cast<UBGraphicsScene*>(QGraphicsItem::scene());
    return scenePtr ? scenePtr->shared_from_this() : nullptr;
}



UBGraphicsPixmapItem* UBGraphicsSvgItem::toPixmapItem() const
{
    QImage image(renderer()->viewBox().size(), QImage::Format_ARGB32);
    QPainter painter(&image);
    renderer()->render(&painter);

    UBGraphicsPixmapItem *pixmapItem =  new UBGraphicsPixmapItem();
    pixmapItem->setPixmap(QPixmap::fromImage(image));

    pixmapItem->setPos(this->pos());
    pixmapItem->setTransform(this->transform());
    pixmapItem->setFlag(QGraphicsItem::ItemIsMovable, true);
    pixmapItem->setFlag(QGraphicsItem::ItemIsSelectable, true);
    pixmapItem->setData(UBGraphicsItemData::ItemLayerType, this->data(UBGraphicsItemData::ItemLayerType));

    return pixmapItem;
}
