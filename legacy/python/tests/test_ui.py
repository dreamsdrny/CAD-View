import os

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtCore import QPoint, Qt
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QFileDialog
import pytest

from cad_app.app import MainWindow, STYLE
from cad_app.model import Document


@pytest.fixture(scope="module")
def app():
    instance = QApplication.instance() or QApplication([])
    instance.setStyleSheet(STYLE)
    return instance


@pytest.fixture
def window(app):
    window = MainWindow(load_demo=False)
    window.show()
    app.processEvents()
    yield window
    for i in range(window.tabs.count()):
        window.tabs.widget(i).document.mark_saved()
    window.close()
    app.processEvents()


def command(window, text):
    window.command_input.setText(text)
    window.execute_command()


def test_commands_selection_move_copy_and_history(window, app):
    command(window, "LINE")
    command(window, "0,0")
    command(window, "@20,0")
    assert window.document.entities[0].points == [(0, 0), (20, 0)]
    window.canvas.set_tool("select")
    key = window.document.entities[0].id
    window.canvas.select_ids({key})
    command(window, "COPY")
    command(window, "0,0")
    command(window, "0,10")
    assert len(window.document.entities) == 2
    assert window.document.entities[1].points == [(0, 10), (20, 10)]
    command(window, "UNDO")
    assert len(window.document.entities) == 1
    command(window, "REDO")
    assert len(window.document.entities) == 2
    window.canvas.grab()
    assert "长度" in [window.property_table.item(i, 0).text() for i in range(window.property_table.rowCount())] or not window.canvas.selected_ids


def test_mouse_drawing_and_measurement(window, app):
    canvas = window.canvas
    canvas.snap_enabled = False
    canvas.set_tool("circle")
    center = canvas.viewport().rect().center()
    QTest.mouseClick(canvas.viewport(), Qt.MouseButton.LeftButton, pos=center)
    QTest.mouseClick(canvas.viewport(), Qt.MouseButton.LeftButton, pos=center + QPoint(50, 0))
    assert len(window.document.entities) == 1
    assert window.document.entities[0].kind == "circle"
    command(window, "DIST")
    command(window, "0,0")
    command(window, "3,4")
    assert "5.0000" in window.console.toPlainText()
    canvas.select_ids({window.document.entities[0].id})
    assert "半径" in [window.property_table.item(i, 0).text() for i in range(window.property_table.rowCount())]
    canvas.grab()


def test_layer_visibility_undo_and_multidocument(window, app):
    command(window, "REC")
    command(window, "0,0")
    command(window, "10,10")
    item = window.layer_table.item(0, 1)
    item.setCheckState(Qt.CheckState.Unchecked)
    assert not window.canvas.items_by_id
    window.history(False)
    assert len(window.canvas.items_by_id) == 1
    previous = window.document
    window.add_document(Document("第二张图纸"))
    app.processEvents()
    assert window.tabs.count() == 2
    command(window, "CIRCLE")
    command(window, "0,0")
    command(window, "5,0")
    assert window.document.entities[0].kind == "circle"
    window.tabs.setCurrentIndex(0)
    assert window.document is previous
    assert window.document.entities[0].kind == "rectangle"


def test_pdf_export_and_gui_save(window, tmp_path, monkeypatch):
    from cad_app.fileio import read_dxf
    command(window, "REC")
    command(window, "0,0")
    command(window, "10,10")
    key = window.document.entities[0].id
    window.canvas.select_ids({key})
    pdf = tmp_path / "drawing.pdf"
    monkeypatch.setattr(QFileDialog, "getSaveFileName", lambda *args, **kwargs: (str(pdf), "PDF (*.pdf)"))
    window.export_pdf()
    assert pdf.read_bytes().startswith(b"%PDF")
    assert window.canvas.selected_ids == {key}
    dxf = tmp_path / "drawing.dxf"
    monkeypatch.setattr(QFileDialog, "getSaveFileName", lambda *args, **kwargs: (str(dxf), "DXF 文件 (*.dxf)"))
    assert window.save_file()
    assert not window.document.modified
    assert read_dxf(dxf).entities[0].properties()["面积"] == pytest.approx(100)
