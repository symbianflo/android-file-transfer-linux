/*
    This file is part of Android File Transfer For Linux.
    Copyright (C) 2015-2017  Vladimir Menshakov

    Android File Transfer For Linux is free software: you can redistribute
    it and/or modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    Android File Transfer For Linux is distributed in the hope that it will
    be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Android File Transfer For Linux.
    If not, see <http://www.gnu.org/licenses/>.
 */

#include "commandqueue.h"
#include "mtpobjectsmodel.h"
#include "utils.h"
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QApplication>

void FinishQueue::execute(CommandQueue &queue)
{ queue.finish(DirectoryId); }

void UploadFile::execute(CommandQueue &queue)
{ queue.uploadFile(Filename); }

void MakeDirectory::execute(CommandQueue &queue)
{ queue.createDirectory(Filename); }

void DownloadFile::execute(CommandQueue &queue)
{ queue.downloadFile(Filename, ObjectId); }

void CommandQueue::downloadFile(const QString &filename, mtp::ObjectId objectId)
{
	if (_aborted)
		return;
	qDebug() << "downloading " << objectId << "to" << filename;

	QFileInfo fi(filename);
	QDir().mkpath(fi.dir().path());
	start(fi.fileName());
	try
	{
		model()->downloadFile(filename, objectId);
	} catch(const std::exception &ex)
	{ qDebug() << "downloading file " << filename << " failed: " << fromUtf8(ex.what()); }

	addProgress(fi.size());
}

void CommandQueue::uploadFile(const QString &filename)
{
	if (_aborted)
		return;

	qDebug() << "uploading file " << filename;

	QFileInfo fi(filename);
	QString parentPath = fi.dir().path();
	if (_directories.empty())
		_directories[parentPath] = _model->parentObjectId();
	start(fi.fileName());
	auto parent = _directories.find(parentPath);
	if (parent == _directories.end())
	{
		qWarning() << "invalid parent " << parentPath;
		return;
	}
	try
	{
		_model->uploadFile(parent.value(), filename);
	} catch(const std::exception &ex)
	{ qDebug() << "uploading file " << filename << " failed: " << fromUtf8(ex.what()); }

	addProgress(fi.size());
}

void CommandQueue::createDirectory(const QString &srcPath)
{
	if (_aborted)
		return;

	QDir dir(srcPath);
	QString path = dir.path();
	qDebug() << "making directory" << path;
	QDir parentDir(path);
	Q_ASSERT(parentDir.cdUp());
	QString parentPath = parentDir.path();
	qDebug() << "parent: " << parentPath << ", dir: " << dir.dirName();
	if (_directories.empty())
		_directories[parentPath] = _model->parentObjectId();

	auto parent = _directories.find(parentPath);
	if (parent == _directories.end())
	{
		qWarning() << "invalid parent " << parentPath;
		return;
	}

	try
	{
		mtp::ObjectId dirId = _model->createDirectory(parent.value(), dir.dirName());
		_directories[path] = dirId;
	} catch(const std::exception &ex)
	{ qDebug() << "creating directory" << path << "failed: " << fromUtf8(ex.what()); return; }
}

CommandQueue::CommandQueue(MtpObjectsModel *model): _model(model), _completedFilesSize(0), _aborted(false)
{
	connect(_model, SIGNAL(filePositionChanged(qint64,qint64)), this, SLOT(onFileProgress(qint64,qint64)));
	qDebug() << "upload worker started";
}

CommandQueue::~CommandQueue()
{
	qDebug() << "upload worker stopped";
}

void CommandQueue::execute(Command *ptr)
{
	std::unique_ptr<Command> cmd(ptr);
	cmd->execute(*this);
}

void CommandQueue::start(const QString &filename)
{
	emit started(filename);
}

void CommandQueue::finish(mtp::ObjectId directoryId)
{
	qDebug() << "finishing queue";
	try
	{
		model()->setParent(directoryId);
	} catch(const std::exception &ex)
	{ qDebug() << "finalizing commands failed: " << fromUtf8(ex.what()); }

	_model->moveToThread(QApplication::instance()->thread());
	_completedFilesSize = 0;
	_directories.clear();
	_aborted = false;
	emit finished();
}

void CommandQueue::abort()
{
	qDebug() << "aborting...";
	_aborted = true;
	_model->session()->AbortCurrentTransaction(6000);
	qDebug() << "sent abort request";
}

void CommandQueue::addProgress(qint64 fileSize)
{
	_completedFilesSize += fileSize;
	emit progress(_completedFilesSize);
}

void CommandQueue::onFileProgress(qint64 pos, qint64)
{
	//qDebug() << "on file progress " << _completedFilesSize << " " << pos;
	emit progress(_completedFilesSize + pos);
}
