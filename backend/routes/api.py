from flask import Blueprint, jsonify, request
from datetime import datetime, timezone
from models import db, Device, Group, Notification

api_bp = Blueprint("api", __name__, url_prefix="/api")


@api_bp.route("/ping")
def ping():
    return jsonify({"status": "ok"})


# ── Devices ──────────────────────────────────────────────────────────────────

@api_bp.route("/devices", methods=["GET"])
def get_devices():
    return jsonify([d.to_dict() for d in Device.query.all()])


@api_bp.route("/devices", methods=["POST"])
def add_device():
    body = request.get_json(force=True)
    name = (body.get("name") or "").strip()
    mac  = (body.get("mac")  or "").strip().upper()

    if not name or len(mac) != 17:
        return jsonify({"error": "Name und gültige MAC erforderlich"}), 400

    if Device.query.filter_by(mac=mac).first():
        return jsonify({"error": "MAC bereits registriert"}), 409

    device = Device(name=name, mac=mac)
    db.session.add(device)
    db.session.commit()
    return jsonify(device.to_dict()), 201


@api_bp.route("/devices/<int:device_id>", methods=["DELETE"])
def delete_device(device_id):
    device = Device.query.get(device_id)
    if not device:
        return jsonify({"error": "Gerät nicht gefunden"}), 404
    db.session.delete(device)
    db.session.commit()
    return "", 204


# ── Groups ───────────────────────────────────────────────────────────────────

@api_bp.route("/groups", methods=["GET"])
def get_groups():
    return jsonify([g.to_dict() for g in Group.query.all()])


@api_bp.route("/groups", methods=["POST"])
def add_group():
    body = request.get_json(force=True)
    name = (body.get("name") or "").strip()

    if not name:
        return jsonify({"error": "Name erforderlich"}), 400

    if Group.query.filter_by(name=name).first():
        return jsonify({"error": "Gruppe existiert bereits"}), 409

    device_ids = body.get("device_ids", [])
    devices = Device.query.filter(Device.id.in_(device_ids)).all() if device_ids else []
    group = Group(name=name, devices=devices)
    db.session.add(group)
    db.session.commit()
    return jsonify(group.to_dict()), 201


@api_bp.route("/groups/<int:group_id>", methods=["PUT"])
def rename_group(group_id):
    group = Group.query.get(group_id)
    if not group:
        return jsonify({"error": "Gruppe nicht gefunden"}), 404

    body = request.get_json(force=True)
    name = (body.get("name") or "").strip()
    if not name:
        return jsonify({"error": "Name erforderlich"}), 400

    group.name = name
    db.session.commit()
    return jsonify(group.to_dict())


@api_bp.route("/groups/<int:group_id>", methods=["DELETE"])
def delete_group(group_id):
    group = Group.query.get(group_id)
    if not group:
        return jsonify({"error": "Gruppe nicht gefunden"}), 404
    db.session.delete(group)
    db.session.commit()
    return "", 204


@api_bp.route("/groups/<int:group_id>/devices", methods=["POST", "PUT"])
def assign_devices(group_id):
    group = Group.query.get(group_id)
    if not group:
        return jsonify({"error": "Gruppe nicht gefunden"}), 404

    body = request.get_json(force=True)
    device_ids = body.get("device_ids", [])

    if not device_ids:
        return jsonify({"error": "Mindestens ein Gerät erforderlich"}), 400

    group.devices = Device.query.filter(Device.id.in_(device_ids)).all()
    db.session.commit()
    return jsonify(group.to_dict())


# ── Notifications ─────────────────────────────────────────────────────────────

@api_bp.route("/notifications", methods=["GET"])
def get_notifications():
    return jsonify([n.to_dict() for n in Notification.query.order_by(Notification.id.desc()).all()])


@api_bp.route("/notifications", methods=["POST"])
def add_notification():
    body = request.get_json(force=True)
    message  = (body.get("message") or "").strip()
    group_id = body.get("group_id")

    if not message:
        return jsonify({"error": "Nachricht erforderlich"}), 400
    if not group_id:
        return jsonify({"error": "group_id erforderlich"}), 400

    group = Group.query.get(group_id)
    if not group:
        return jsonify({"error": "Gruppe nicht gefunden"}), 404

    ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    notification = Notification(message=message, group_id=group_id, timestamp=ts)
    db.session.add(notification)
    db.session.commit()
    return jsonify(notification.to_dict()), 201
