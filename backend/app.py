from flask import Flask
from flask_cors import CORS
from models import db
from routes.admin import admin_bp
from routes.api import api_bp
import os


def create_app():
    app = Flask(__name__)

    base_dir = os.path.abspath(os.path.dirname(__file__))
    app.config["SQLALCHEMY_DATABASE_URI"] = f"sqlite:///{os.path.join(base_dir, 'smartserve.db')}"
    app.config["SQLALCHEMY_TRACK_MODIFICATIONS"] = False

    db.init_app(app)
    CORS(app, resources={r"/api/*": {"origins": "*"}})

    app.register_blueprint(admin_bp)
    app.register_blueprint(api_bp)

    with app.app_context():
        db.create_all()

    return app


app = create_app()

if __name__ == "__main__":
    app.run(debug=True)
