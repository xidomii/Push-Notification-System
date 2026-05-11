from flask_sqlalchemy import SQLAlchemy

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
        return {
            "id":        self.id,
            "name":      self.name,
            "mac":       self.mac,
            "status":    self.status,
            "last_seen": self.last_seen,
        }


class Group(db.Model):
    id      = db.Column(db.Integer, primary_key=True)
    name    = db.Column(db.String(100), nullable=False, unique=True)
    devices = db.relationship("Device", secondary=device_group, backref="groups")

    def to_dict(self):
        return {
            "id":      self.id,
            "name":    self.name,
            "devices": [d.to_dict() for d in self.devices],
        }
