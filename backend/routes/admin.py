from flask import Blueprint, render_template, send_from_directory
from models import Device, Group
import os

admin_bp = Blueprint("admin", __name__)

FRONTEND_TEMPLATES = os.path.join(os.path.dirname(__file__), "..", "..", "frontend", "templates")


@admin_bp.route("/")
@admin_bp.route("/dashboard")
def dashboard():
    return render_template("dashboard.html", device_count=len(Device.query.all()), group_count=len(Group.query.all()))


@admin_bp.route("/devices")
def devices():
    return send_from_directory(FRONTEND_TEMPLATES, "devices.html")


@admin_bp.route("/groups")
def groups():
    return send_from_directory(FRONTEND_TEMPLATES, "groups.html")


@admin_bp.route("/notifications")
def notifications():
    return render_template("notifications.html")
