#include "DragAndDropList.h"

#include <QUrl>
#include<QFileInfo>

DragAndDropList::DragAndDropList(QWidget* parent)
	: QListWidget(parent) {
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDropIndicatorShown(true);
    setDefaultDropAction(Qt::CopyAction);
}

void DragAndDropList::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
    else {
        QListWidget::dragEnterEvent(event);
    }
}

void DragAndDropList::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasUrls()) {
        const auto urls = event->mimeData()->urls();
        for (const QUrl& url : urls) {
            QString filePath = url.toLocalFile();
            if (!filePath.isEmpty()) {
                QFileInfo fileInfo(filePath);
                if (fileInfo.exists() && fileInfo.isFile()) {
                    addItem(filePath);
                }
            }
        }
        event->acceptProposedAction();
    }
    else {
        QListWidget::dropEvent(event);
    }
}