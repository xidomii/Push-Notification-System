from flask import Blueprint, render_template
from models import Device, Group

admin_bp = Blueprint("admin", __name__)


@admin_bp.route("/")
@admin_bp.route("/dashboard")
def dashboard():
    return render_template("dashboard.html", device_count=len(Device.query.all()), group_count=len(Group.query.all()))


@admin_bp.route("/devices")
def devices():
    return render_template("devices.html", devices=Device.query.all())


@admin_bp.route("/groups")
def groups():
    return render_template("groups.html", groups=Group.query.all(), devices=Device.query.all())


@admin_bp.route("/notifications")
def notifications():
    return render_template("notifications.html")
