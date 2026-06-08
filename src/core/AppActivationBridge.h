#pragma once

#include <QObject>

class QCoreApplication;
class QEvent;

namespace compactphone {

class AppActivationBridge : public QObject {
    Q_OBJECT
public:
    explicit AppActivationBridge(QCoreApplication *app, QObject *parent = nullptr);
    ~AppActivationBridge() override;

public slots:
    void requestRestore();

signals:
    void restoreRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QCoreApplication *m_app = nullptr;
};

void installMacDockReopenHandler(AppActivationBridge *bridge);
void clearMacDockReopenHandler(AppActivationBridge *bridge);

} // namespace compactphone
