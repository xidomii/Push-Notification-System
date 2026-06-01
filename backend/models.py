from flask_sqlalchemy import SQLAlchemy
from datetime import datetime, timezone, timedelta

db = SQLAlchemy()

device_group = db.Table(
    "device_group",
    db.Column("device_id", db.Integer, db.ForeignKey("device.id"), primary_key=True),
    db.Column("group_id",  db.Integer, db.ForeignKey("group.id"),  primary_key=True),
)


class Device(db.Model):
    id        = db.Column(db.Integer, primary_key=True)
    name      = db.Column(db.String(100), nullable=False)
    mac       = db.Column(db.String(17), unique=True, nullable=False)
    status    = db.Column(db.String(10), default="offline")
    last_seen = db.Column(db.String(30), nullable=True)

    def to_dict(self):
        status = "offline"
        if self.last_seen:
            try:
                ls = datetime.fromisoformat(self.last_seen.replace("Z", "+00:00"))
                if (datetime.now(timezone.utc) - ls) < timedelta(seconds=60):
                    status = "online"
            except Exception:
                status = self.status or "offline"
        return {
            "id":        self.id,
            "name":      self.name,
            "mac":       self.mac,
            "status":    status,
            "last_seen": self.last_seen,
        }


class Notification(db.Model):
    id        = db.Column(db.Integer, primary_key=True)
    message   = db.Column(db.String(500), nullable=False)
    timestamp = db.Column(db.String(30), nullable=False)
    group_id  = db.Column(db.Integer, db.ForeignKey("group.id"), nullable=False)
    group     = db.relationship("Group", backref="notifications")

    def to_dict(self):
        return {
            "id":         self.id,
            "message":    self.message,
            "timestamp":  self.timestamp,
            "group_id":   self.group_id,
            "group_name": self.group.name if self.group else None,
        }


class Group(db.Model):
    id      = db.Column(db.Integer, primary_key=True)
    name    = db.Column(db.String(100), nullable=False, unique=True)
    devices = db.relationship("Device", secondary=device_group, backref="groups")

    def to_dict(self):
        return {
            "id":         self.id,
            "name":       self.name,
            "device_ids": [d.id for d in self.devices],
        }
