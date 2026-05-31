#include "LinesController.h"

#include "LinesManager.h"
#include "WatchedLine.h"
#include "models/LinesModel.h"

#include <utility>

namespace compactphone {

LinesController::LinesController(sip::LinesManager *lines,
                                models::LinesModel *model,
                                ActiveAccountProvider activeAccount,
                                QObject *parent)
    : QObject(parent), m_lines(lines), m_model(model),
      m_activeAccount(std::move(activeAccount)) {}

LinesController::~LinesController() = default;

QAbstractListModel *LinesController::model() const { return m_model; }

int LinesController::add(const QString &uri, const QString &label)
{
    if (!m_lines) return -1;
    const int aid = m_activeAccount ? m_activeAccount() : -1;
    if (aid <= 0) return -1;
    return m_lines->add(aid, uri.toStdString(), label.toStdString());
}

bool LinesController::remove(int lineId)
{
    return m_lines && m_lines->remove(static_cast<sip::WatchedLineId>(lineId));
}

void LinesController::dial(int lineId)
{
    if (!m_lines) return;
    if (auto l = m_lines->find(static_cast<sip::WatchedLineId>(lineId))) {
        emit callRequested(QString::fromStdString(l->uri));
    }
}

} // namespace compactphone
