from flask import Blueprint, send_from_directory
import os

admin_bp = Blueprint("admin", __name__)

FRONTEND_TEMPLATES = os.path.join(os.path.dirname(__file__), "..", "..", "frontend", "templates")


@admin_bp.route("/")
@admin_bp.route("/dashboard")
def dashboard():
    return send_from_directory(FRONTEND_TEMPLATES, "dashboard.html")


@admin_bp.route("/devices")
def devices():
    return send_from_directory(FRONTEND_TEMPLATES, "devices.html")


@admin_bp.route("/groups")
def groups():
    return send_from_directory(FRONTEND_TEMPLATES, "groups.html")


@admin_bp.route("/notifications")
def notifications():
    return send_from_directory(FRONTEND_TEMPLATES, "notifications.html")
