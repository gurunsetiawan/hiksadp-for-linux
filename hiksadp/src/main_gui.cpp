#include "ui/main_window.hpp"
#include "core/logger.hpp"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    hiksadp::Logger::set_log_file("logs/hiksadp.log");
    hiksadp::Logger::write(hiksadp::LogLevel::Info, "GUI started");
    hiksadp::ui::MainWindow window;
    window.show();
    return app.exec();
}
