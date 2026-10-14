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




#ifndef UBGRAPHICSSVGITEM_H_
#define UBGRAPHICSSVGITEM_H_

#include <QtGui>
#include <QColor>
#include <QDomElement>
#include <QtSvg>

#include "UBMediaAssetItem.h"

#include "core/UB.h"

class UBGraphicsItemDelegate;
class UBGraphicsPixmapItem;

class UBGraphicsSvgItem: public QGraphicsSvgItem, public UBMediaAssetItem, public UBGraphicsItem
{
    public:
        UBGraphicsSvgItem(const QString& pFile, QGraphicsItem* parent = 0);
        UBGraphicsSvgItem(const QByteArray& pFileData, QGraphicsItem* parent = 0);

        void init();

        virtual ~UBGraphicsSvgItem();

        virtual QList<QString> mediaAssets() const override;
        virtual void setMediaAsset(const QString& documentPath, const QString& mediaAsset) override;

        QByteArray fileData() const;

        // Shapes from the library carry their geometry in a single group marked
        // ub-body, with the colours on that group. Anything else, an imported
        // drawing say, has no such group and is left alone.
        bool isRecolourable() const;
        QColor shapeFill() const;
        QColor shapeStroke() const;
        bool setShapeColors(const QColor& fill, const QColor& stroke);

        // Colours round-trip through settings as text; an invalid colour, which
        // is how "no fill" is expressed, stores as none.
        static QString colourToSetting(const QColor& colour);
        static QColor colourFromSetting(const QString& value);

        QString shapeText() const;
        void setShapeText(const QString& text);
        QColor shapeTextColor() const;
        void setShapeTextColor(const QColor& colour);

        // Puts the caret in the label and lets it be typed into directly,
        // rather than through a dialog.
        void beginLabelEdit();
        // Called by the label when editing finishes or its content changes.
        void labelEdited();
        void labelEditFinished();

        enum { Type = UBGraphicsItemType::SvgItemType };

        virtual int type() const override
        {
            return Type;
        }

        virtual UBItem* deepCopy() const override;

        virtual void copyItemParameters(UBItem *copy) const override;

        virtual void setRenderingQuality(RenderingQuality pRenderingQuality) override;

        virtual std::shared_ptr<UBGraphicsScene> scene() override;

        virtual UBGraphicsPixmapItem* toPixmapItem() const;

    protected:

        virtual void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
        virtual void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
        virtual void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

        virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

        virtual QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

        static QDomElement findShapeBody(const QDomElement& element);
        void updateShapeTextLayout();

        QByteArray mFileData;
        QUuid mMediaAssetUuid;
        // Renderers built by setShapeColors(), freed as they are superseded.
        QSvgRenderer* mOwnedRenderer{nullptr};

        QString mShapeText;
        QColor mShapeTextColor{Qt::black};
        // A plain QGraphicsTextItem subclass on purpose: UBGraphicsTextItem is a
        // type the document writer knows, and would be saved as an item in its
        // own right.
        class LabelItem;
        LabelItem* mTextItem{nullptr};
};

#endif /* UBGRAPHICSSVGITEM_H_ */
