#pragma once
#ifndef DRAGANDDROPLIST_H
#define DRAGANDDROPLIST_H

#include<QListWidget>
#include<QDropEvent>
#include<QDragEnterEvent>
#include<QMimeData>

class DragAndDropList : public QListWidget {
	Q_OBJECT

public:
	explicit DragAndDropList(QWidget* parent = nullptr);

private:
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;
};
#endif