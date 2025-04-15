#include "DragAndDropList.h"

#include <QUrl>

DragAndDropList::DragAndDropList(QWidget* parent)
	: QListWidget(parent) {
	setAcceptDrops(true);
}

void DragAndDropList::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void DragAndDropList::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) {
            addItem(url.toLocalFile());
        }
    }
    event->acceptProposedAction();
}