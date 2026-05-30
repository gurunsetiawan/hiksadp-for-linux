#include "ui/main_window.hpp"
#include "core/logger.hpp"

#include <QApplication>
#include <QDir>
#include <QStandardPaths>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    const auto log_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir{}.mkpath(log_dir);
    hiksadp::Logger::set_log_file((log_dir + "/hiksadp.log").toStdString());
    hiksadp::Logger::write(hiksadp::LogLevel::Info, "GUI started");
    hiksadp::ui::MainWindow window;
    window.show();
    return app.exec();
}
